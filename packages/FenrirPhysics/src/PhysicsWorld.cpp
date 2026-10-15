#include "FenrirPhysics/PhysicsWorld.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirLogger/ILogger.hpp"
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
            if (a.Is<ConvexHullShape>())
            {
                return a.As<ConvexHullShape>().hull == b.As<ConvexHullShape>().hull;
            }
            if (a.Is<MeshShape>())
            {
                return a.As<MeshShape>().mesh == b.As<MeshShape>().mesh;
            }
            if (a.Is<CapsuleShape>())
            {
                const CapsuleShape& x = a.As<CapsuleShape>();
                const CapsuleShape& y = b.As<CapsuleShape>();
                return Same(x.halfHeight, y.halfHeight) && Same(x.radius, y.radius);
            }
            return Same(a.As<BoxShape>().halfExtents, b.As<BoxShape>().halfExtents);
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

    PhysicsWorld::Impl::Impl(EntityList& entities, const Backend::WorldSettings& settings,
                             Backend::CollisionMeshes& meshes, ILogger& logger)
        : m_entities(&entities), m_logger(&logger), m_backend(settings, meshes, logger),
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
        CollectUnmadeShapes(changed);
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
            const CollisionShape& shape = entity.GetComponent<Collider>().shape;

            Backend::BodyDesc desc;
            desc.motion = MotionOf(rigidBody);
            desc.canMove = rigidBody != nullptr;
            desc.shape = shape;
            desc.scale = world.scale;
            desc.position = world.pos;
            desc.rotation = world.rot;
            if (rigidBody != nullptr)
            {
                desc.linearVelocity = rigidBody->linearVelocity;
                desc.angularVelocity = rigidBody->angularVelocity;
            }
            desc.entity = id;
            if (!m_backend.CanMakeShape(shape))
            {
                m_logger->Warn("The collider of entity " + std::to_string(id) +
                               " refers to a collision mesh that is released or cannot be used as its kind of shape; "
                               "its body is inactive until its shape changes");
                m_bodies.emplace(id, Record{Backend::BodyId{}, desc.canMove, desc.motion, desc.shape,
                                            Transform(desc.position, desc.rotation, desc.scale), desc.linearVelocity,
                                            desc.angularVelocity});
                continue;
            }
            entities.push_back(id);
            descs.push_back(desc);
        }

        std::vector<Backend::BodyId> ids(descs.size());
        m_backend.CreateBodies(descs, ids);
        for (std::size_t i = 0; i < descs.size(); ++i)
        {
            const Backend::BodyDesc& desc = descs[i];
            m_bodies.emplace(entities[i], Record{ids[i], desc.canMove, desc.motion, desc.shape,
                                                 Transform(desc.position, desc.rotation, desc.scale),
                                                 desc.linearVelocity, desc.angularVelocity});
        }
    }

    void PhysicsWorld::Impl::CollectUnmadeShapes(std::set<std::uint32_t>& changed)
    {
        // Rebuilt: bodies whose collision mesh was released since (or whose shape was changed to one that cannot be
        // made), and inactive colliders whose shape has changed since, which may now be made.
        for (const auto& [id, record] : m_bodies)
        {
            if (changed.contains(id) || !m_entities->HasEntity(id))
            {
                continue; // already rebuilt, or gone
            }
            const CollisionShape& shape = m_entities->GetEntity(id).GetComponent<Collider>().shape;
            const bool active = record.id.value != Backend::BodyId{}.value;
            if (active ? !m_backend.CanMakeShape(shape) : !Same(shape, record.shape))
            {
                changed.insert(id);
            }
        }
    }

    void PhysicsWorld::Impl::PushEdits(float dt)
    {
        for (auto& [id, record] : m_bodies)
        {
            if (record.id.value == Backend::BodyId{}.value)
            {
                continue; // inactive: no body to push to
            }
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

            if (motion.motionType == MotionType::Kinematic)
            {
                // Every step, even unmoved: otherwise the body keeps its last velocity and drifts.
                m_backend.MoveKinematic(record.id, world.pos, world.rot, dt);
            }
            else if (!Same(world.pos, record.world.pos) || !Same(world.rot, record.world.rot))
            {
                m_backend.Teleport(record.id, world.pos, world.rot);
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
            if (record.motion.motionType != MotionType::Dynamic || record.id.value == Backend::BodyId{}.value)
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
