#include "FenrirPhysics/PhysicsWorld.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <vector>

#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirScene/Scene.hpp"
#include "WorldImpl.hpp"

namespace Fenrir
{
    namespace
    {
        // Bit for bit: any value game code writes is an edit, and a value physics wrote back is not.
        bool Same(float a, float b)
        {
            return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
        }

        bool Same(const Math::Vec3& a, const Math::Vec3& b)
        {
            return Same(a.x, b.x) && Same(a.y, b.y) && Same(a.z, b.z);
        }

        bool Same(const Math::Quat& a, const Math::Quat& b)
        {
            return Same(a.x, b.x) && Same(a.y, b.y) && Same(a.z, b.z) && Same(a.w, b.w);
        }

        bool Same(const Backend::Motion& a, const Backend::Motion& b)
        {
            return a.motionType == b.motionType && Same(a.mass, b.mass) && Same(a.linearDamping, b.linearDamping) &&
                   Same(a.angularDamping, b.angularDamping) && Same(a.gravityScale, b.gravityScale);
        }

        bool Same(const CollisionShape& a, const CollisionShape& b)
        {
            if (a.Type() != b.Type())
            {
                return false;
            }
            if (a.Is<SphereShape>())
            {
                return Same(a.As<SphereShape>().radius, b.As<SphereShape>().radius);
            }
            return Same(a.As<BoxShape>().halfExtents, b.As<BoxShape>().halfExtents);
        }

        bool Same(const PhysicsMaterial& a, const PhysicsMaterial& b)
        {
            return Same(a.friction, b.friction) && Same(a.restitution, b.restitution);
        }

        PhysicsMaterial MaterialOf(Entity& entity)
        {
            return entity.HasComponent<PhysicsMaterial>() ? entity.GetComponent<PhysicsMaterial>() : PhysicsMaterial{};
        }

        bool Same(const CollisionFilter& a, const CollisionFilter& b)
        {
            return a.layers == b.layers && a.mask == b.mask;
        }

        // Read from the body's entity: it applies to all the body's colliders.
        CollisionFilter FilterOf(Entity& entity)
        {
            return entity.HasComponent<CollisionFilter>() ? entity.GetComponent<CollisionFilter>() : CollisionFilter{};
        }

        Backend::Motion MotionOf(const RigidBody* rigidBody)
        {
            if (rigidBody == nullptr)
            {
                return Backend::Motion{};
            }
            return Backend::Motion{rigidBody->motionType, rigidBody->mass, rigidBody->linearDamping,
                                   rigidBody->angularDamping, rigidBody->gravityScale};
        }

        RigidBody* RigidBodyOf(Entity& entity)
        {
            return entity.HasComponent<RigidBody>() ? &entity.GetComponent<RigidBody>() : nullptr;
        }

        bool Same(const Transform& a, const Transform& b)
        {
            return Same(a.pos, b.pos) && Same(a.rot, b.rot) && Same(a.scale, b.scale);
        }

        // How many ancestors the entity has: blending writes parents before children.
        int DepthOf(const Entity& entity)
        {
            int depth = 0;
            for (Entity at = entity;
                 at.HasComponent<Relationship>() && at.GetComponent<Relationship>().parent.IsValid();
                 at = at.GetComponent<Relationship>().parent)
            {
                ++depth;
            }
            return depth;
        }

        // Enough collision steps that none is longer than a 60 Hz step.
        int CollisionSteps(float dt)
        {
            return std::max(1, static_cast<int>(std::ceil(dt * 60.0f - 1e-4f)));
        }
    } // namespace

    PhysicsWorld::PhysicsWorld(std::unique_ptr<Impl> impl) : m_impl(std::move(impl))
    {
    }

    PhysicsWorld::~PhysicsWorld() = default;

    PhysicsWorld* PhysicsWorld::Of(Scene& scene)
    {
        return scene.GetResource<PhysicsWorld>();
    }

    PhysicsWorld::Impl::Impl(EntityList& entities, const Backend::WorldSettings& settings, ILogger& logger)
        : m_entities(&entities), m_backend(settings, logger),
          m_rigidBodies(entities.Watch(ComponentType::Native<RigidBody>())),
          m_colliders(entities.Watch(ComponentType::Native<Collider>()))
    {
        // A watch records only what happens after it is made: the colliders already there are found once, here.
        const ComponentType colliders[] = {ComponentType::Native<Collider>()};
        entities.ForEachWith(colliders, [this](std::uint32_t entity) { m_pending.insert(entity); });
    }

    void PhysicsWorld::Impl::Step(float dt)
    {
        SyncBodies();
        PushEdits(dt);
        m_backend.Step(dt, CollisionSteps(dt));
        WriteBack();
        AdvancePoses();
    }

    void PhysicsWorld::Impl::AdvancePoses()
    {
        for (auto& [id, record] : m_bodies)
        {
            record.previous = record.snap ? record.world : record.current;
            record.current = record.world;
            record.snap = false;
            record.heldByGame = false;
        }
    }

    void PhysicsWorld::Impl::Restore()
    {
        for (auto& [id, record] : m_bodies)
        {
            if (!record.blended)
            {
                continue;
            }
            record.blended = false;
            if (!m_entities->HasEntity(id))
            {
                continue;
            }
            Transform& local = m_entities->GetEntity(id).GetComponent<Transform>();
            if (Same(local, record.shownLocal))
            {
                local = record.trueLocal;
            }
            else
            {
                // Game code moved it during Update: its value stays, and nothing blends across the jump.
                record.snap = true;
                record.heldByGame = true;
            }
        }
    }

    void PhysicsWorld::Impl::Blend(float fraction)
    {
        struct Blended
        {
            int depth;
            Entity entity;
            Record* record;
        };
        std::vector<Blended> blended;
        for (auto& [id, record] : m_bodies)
        {
            if (record.motion.motionType == MotionType::Static || record.heldByGame || !m_entities->HasEntity(id))
            {
                continue;
            }
            Entity entity = m_entities->GetEntity(id);
            const RigidBody* rigidBody = RigidBodyOf(entity);
            if (rigidBody == nullptr || !rigidBody->interpolate || !entity.HasComponent<Transform>())
            {
                continue;
            }
            blended.push_back(Blended{DepthOf(entity), entity, &record});
        }
        // Parents before children, so each child's world pose is set against its parent's blended one.
        std::stable_sort(blended.begin(), blended.end(),
                         [](const Blended& a, const Blended& b) { return a.depth < b.depth; });

        // Every true local Transform is saved before any is overwritten.
        for (const Blended& body : blended)
        {
            body.record->trueLocal = body.entity.GetComponent<Transform>();
        }
        for (const Blended& body : blended)
        {
            Record& record = *body.record;
            Transform world = record.world;
            world.pos = record.previous.pos + (record.current.pos - record.previous.pos) * fraction;
            world.rot = Math::Slerp(record.previous.rot, record.current.rot, fraction);
            SetWorldTransform(body.entity, world);
            record.shownLocal = body.entity.GetComponent<Transform>();
            record.blended = true;
        }
    }

    void PhysicsWorld::Impl::SyncBodies()
    {
        // Only each entity's net effect matters: every entity whose components came or went has its body rebuilt from
        // what it has now (added then removed: no body; removed then added: a new body).
        std::set<std::uint32_t> changed = std::move(m_pending);
        m_pending.clear();
        for (const ComponentChange& change : m_rigidBodies.Drain())
        {
            changed.insert(change.entity);
        }
        for (const ComponentChange& change : m_colliders.Drain())
        {
            changed.insert(change.entity);
        }
        if (changed.empty())
        {
            return;
        }

        std::vector<Backend::BodyId> destroyed;
        for (std::uint32_t entity : changed)
        {
            const auto it = m_bodies.find(entity);
            if (it != m_bodies.end())
            {
                destroyed.push_back(it->second.id);
                m_bodies.erase(it);
            }
        }
        m_backend.DestroyBodies(destroyed);

        std::vector<std::uint32_t> entities;
        std::vector<Backend::BodyDesc> descs;
        for (std::uint32_t id : changed)
        {
            if (!m_entities->HasEntity(id))
            {
                continue;
            }
            Entity entity = m_entities->GetEntity(id);
            if (!entity.HasComponent<Collider>())
            {
                continue;
            }
            const RigidBody* rigidBody = RigidBodyOf(entity);
            const Transform world = GetWorldTransform(entity);

            Backend::BodyDesc desc;
            desc.motion = MotionOf(rigidBody);
            desc.canMove = rigidBody != nullptr;
            desc.shape = entity.GetComponent<Collider>().shape;
            desc.material = MaterialOf(entity);
            desc.filter = FilterOf(entity);
            desc.scale = world.scale;
            desc.position = world.pos;
            desc.rotation = world.rot;
            if (rigidBody != nullptr)
            {
                desc.linearVelocity = rigidBody->linearVelocity;
                desc.angularVelocity = rigidBody->angularVelocity;
            }
            desc.entity = id;
            entities.push_back(id);
            descs.push_back(desc);
        }

        std::vector<Backend::BodyId> ids(descs.size());
        m_backend.CreateBodies(descs, ids);
        for (std::size_t i = 0; i < descs.size(); ++i)
        {
            const Backend::BodyDesc& desc = descs[i];
            m_bodies.emplace(entities[i], Record{ids[i], desc.canMove, desc.motion, desc.shape, desc.material,
                                                 desc.filter, Transform(desc.position, desc.rotation, desc.scale),
                                                 desc.linearVelocity, desc.angularVelocity});
        }
    }

    void PhysicsWorld::Impl::PushEdits(float dt)
    {
        for (auto& [id, record] : m_bodies)
        {
            Entity entity = m_entities->GetEntity(id);
            RigidBody* rigidBody = RigidBodyOf(entity);
            const Backend::Motion motion = MotionOf(rigidBody);
            const CollisionShape& shape = entity.GetComponent<Collider>().shape;
            const Transform world = GetWorldTransform(entity);

            bool pushVelocity = false;
            if (!Same(motion, record.motion))
            {
                m_backend.SetMotion(record.id, motion);
                // A body that becomes dynamic starts with the velocity its RigidBody holds, not one the backend gave it
                // while it was kinematic.
                pushVelocity = motion.motionType == MotionType::Dynamic;
                record.motion = motion;
            }

            if (!Same(shape, record.shape) || !Same(world.scale, record.world.scale))
            {
                m_backend.SetShape(record.id, shape, world.scale, motion.mass);
                record.shape = shape;
            }

            const PhysicsMaterial material = MaterialOf(entity);
            if (!Same(material, record.material))
            {
                m_backend.SetMaterial(record.id, material);
                record.material = material;
            }

            const CollisionFilter filter = FilterOf(entity);
            if (!Same(filter, record.filter))
            {
                m_backend.SetFilter(record.id, filter);
                record.filter = filter;
            }

            if (motion.motionType == MotionType::Kinematic)
            {
                // Every step, even unmoved: otherwise the body keeps its last velocity and drifts.
                m_backend.MoveKinematic(record.id, world.pos, world.rot, dt);
            }
            else if (!Same(world.pos, record.world.pos) || !Same(world.rot, record.world.rot))
            {
                m_backend.Teleport(record.id, world.pos, world.rot);
                record.snap = true;
            }
            record.world = world;

            if (rigidBody != nullptr && motion.motionType == MotionType::Dynamic &&
                (pushVelocity || !Same(rigidBody->linearVelocity, record.linearVelocity) ||
                 !Same(rigidBody->angularVelocity, record.angularVelocity)))
            {
                m_backend.SetVelocity(record.id, rigidBody->linearVelocity, rigidBody->angularVelocity);
                record.linearVelocity = rigidBody->linearVelocity;
                record.angularVelocity = rigidBody->angularVelocity;
            }
        }
    }

    void PhysicsWorld::Impl::WriteBack()
    {
        for (auto& [id, record] : m_bodies)
        {
            if (record.motion.motionType != MotionType::Dynamic)
            {
                continue;
            }
            Entity entity = m_entities->GetEntity(id);
            const Backend::BodyState state = m_backend.GetState(record.id);

            Transform world = record.world;
            world.pos = state.position;
            world.rot = state.rotation;
            SetWorldTransform(entity, world);
            // Read back the way the diff reads, so rounding through a parent is not taken for a game edit.
            record.world = GetWorldTransform(entity);

            RigidBody& rigidBody = entity.GetComponent<RigidBody>();
            rigidBody.linearVelocity = state.linearVelocity;
            rigidBody.angularVelocity = state.angularVelocity;
            record.linearVelocity = state.linearVelocity;
            record.angularVelocity = state.angularVelocity;
        }
    }
} // namespace Fenrir
