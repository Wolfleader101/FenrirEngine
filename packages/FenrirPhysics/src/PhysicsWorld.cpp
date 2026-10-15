// PROTOTYPE (physics spike): the PostTick pipeline. Knows the ECS, not Jolt: it talks only to Backend::World.

#include "FenrirPhysics/Physics.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "Backend/Backend.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirScene/Scene.hpp"

namespace Fenrir
{
    namespace
    {
        // What physics last applied to, or wrote back from, a body: the diff compares against it.
        struct Record
        {
            Backend::BodyId id;
            MotionType motionType;
            float mass;
            CollisionShape shape;
            Transform world;
            Math::Vec3 linearVelocity;
            Math::Vec3 angularVelocity;
        };
    } // namespace

    struct PhysicsWorld::Impl
    {
        explicit Impl(const PhysicsConfig& config)
            : backend(Backend::WorldSettings{config.gravity, config.maxBodies, config.maxBodyPairs, config.maxContacts})
        {
        }

        Backend::World backend;
        bool watching = false;
        ComponentWatch rigidBodies;
        ComponentWatch colliders;
        std::map<std::uint32_t, Record> bodies; // ordered by entity: the diff's visiting order is deterministic

        void Destroy(std::uint32_t entity)
        {
            auto it = bodies.find(entity);
            if (it != bodies.end())
            {
                backend.DestroyBody(it->second.id);
                bodies.erase(it);
            }
        }

        // Spike rule: a body is an entity with a Collider; its RigidBody (if any) gives the motion type.
        // Compound bodies (colliders on child entities) are not handled.
        void Rebuild(EntityList& entities, std::uint32_t id)
        {
            Destroy(id);
            if (!entities.HasEntity(id))
            {
                return;
            }
            Entity entity = entities.GetEntity(id);
            if (!entity.HasComponent<Collider>())
            {
                return;
            }

            const RigidBody* rigidBody = entity.HasComponent<RigidBody>() ? &entity.GetComponent<RigidBody>() : nullptr;
            Record record;
            record.motionType = rigidBody ? rigidBody->motionType : MotionType::Static;
            record.mass = rigidBody ? rigidBody->mass : 0.0f;
            record.shape = entity.GetComponent<Collider>().shape;
            record.world = GetWorldTransform(entity);
            record.linearVelocity = rigidBody ? rigidBody->linearVelocity : Math::Vec3(0.0f);
            record.angularVelocity = rigidBody ? rigidBody->angularVelocity : Math::Vec3(0.0f);

            Backend::BodyDesc desc;
            desc.motionType = record.motionType;
            desc.shape = record.shape;
            desc.scale = record.world.scale;
            desc.position = record.world.pos;
            desc.rotation = record.world.rot;
            desc.linearVelocity = record.linearVelocity;
            desc.angularVelocity = record.angularVelocity;
            desc.mass = record.mass;
            desc.userData = id;
            record.id = backend.CreateBody(desc);
            bodies.emplace(id, record);
        }
    };

    PhysicsWorld::PhysicsWorld(const PhysicsConfig& config) : m_impl(std::make_unique<Impl>(config))
    {
    }

    PhysicsWorld::~PhysicsWorld() = default;

    PhysicsWorld* PhysicsWorld::Of(Scene& scene)
    {
        return scene.GetResource<PhysicsWorld>();
    }

    void PhysicsWorld::SetGravity(const Math::Vec3& gravity)
    {
        m_impl->backend.SetGravity(gravity);
    }

    void PhysicsWorld::Step(Scene& scene, float dt)
    {
        Impl& impl = *m_impl;
        EntityList& entities = scene.GetEntityList();

        // 1. Drain the watches (on the first step, every collider already there counts as added).
        std::set<std::uint32_t> changed;
        if (!impl.watching)
        {
            impl.rigidBodies = entities.Watch<RigidBody>();
            impl.colliders = entities.Watch<Collider>();
            impl.watching = true;
            const ComponentType colliderType[] = {ComponentType::Native<Collider>()};
            entities.ForEachWith(colliderType, [&](std::uint32_t id) { changed.insert(id); });
        }
        for (const auto& change : impl.rigidBodies.Drain())
        {
            changed.insert(change.entity);
        }
        for (const auto& change : impl.colliders.Drain())
        {
            changed.insert(change.entity);
        }
        for (std::uint32_t id : changed)
        {
            impl.Rebuild(entities, id);
        }

        // 2. Diff each body against what physics last applied or wrote back, and push the differences.
        std::vector<std::uint32_t> rebuild;
        for (auto& [id, record] : impl.bodies)
        {
            Entity entity = entities.GetEntity(id);
            const Transform world = GetWorldTransform(entity);
            const CollisionShape& shape = entity.GetComponent<Collider>().shape;
            RigidBody* rigidBody = entity.HasComponent<RigidBody>() ? &entity.GetComponent<RigidBody>() : nullptr;
            const MotionType motionType = rigidBody ? rigidBody->motionType : MotionType::Static;
            const float mass = rigidBody ? rigidBody->mass : 0.0f;

            if (motionType != record.motionType || mass != record.mass)
            {
                rebuild.push_back(id); // spike: a rebuild rather than SetMotionType/SetObjectLayer
                continue;
            }
            if (!(shape == record.shape) || world.scale != record.world.scale)
            {
                impl.backend.SetShape(record.id, shape, world.scale);
                record.shape = shape;
            }
            if (motionType == MotionType::Kinematic)
            {
                // Every step, so a kinematic body that stops being moved also stops.
                impl.backend.MoveKinematic(record.id, world.pos, world.rot, dt);
            }
            else if (world.pos != record.world.pos || world.rot != record.world.rot)
            {
                impl.backend.Teleport(record.id, world.pos, world.rot);
            }
            record.world = world;

            if (rigidBody && motionType == MotionType::Dynamic &&
                (rigidBody->linearVelocity != record.linearVelocity ||
                 rigidBody->angularVelocity != record.angularVelocity))
            {
                impl.backend.SetVelocity(record.id, rigidBody->linearVelocity, rigidBody->angularVelocity);
                record.linearVelocity = rigidBody->linearVelocity;
                record.angularVelocity = rigidBody->angularVelocity;
            }
        }
        for (std::uint32_t id : rebuild)
        {
            impl.Rebuild(entities, id);
        }

        // 3. Forces and impulses: not in the spike.

        // 4. Step.
        const int collisionSteps = std::max(1, static_cast<int>(std::ceil(dt * 60.0f - 1e-4f)));
        impl.backend.Step(dt, collisionSteps);

        // 5. Write back dynamic bodies, and record what was written so it is not taken for a game edit.
        for (auto& [id, record] : impl.bodies)
        {
            if (record.motionType != MotionType::Dynamic)
            {
                continue;
            }
            Entity entity = entities.GetEntity(id);
            const Backend::BodyState state = impl.backend.GetState(record.id);

            Transform world = record.world;
            world.pos = state.position;
            world.rot = state.rotation;
            SetWorldTransform(entity, world);
            record.world = GetWorldTransform(entity); // read back through the same path the diff uses

            RigidBody& rigidBody = entity.GetComponent<RigidBody>();
            rigidBody.linearVelocity = state.linearVelocity;
            rigidBody.angularVelocity = state.angularVelocity;
            record.linearVelocity = state.linearVelocity;
            record.angularVelocity = state.angularVelocity;
        }

        // 6. Events and 7. debug lines: not in the spike.
    }
} // namespace Fenrir
