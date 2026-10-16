#include "FenrirPhysics/PhysicsWorld.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirPhysics/Events.hpp"
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

        // A trigger is a kinematic body whatever RigidBody its entity has: it follows its entity and pushes nothing.
        Backend::Motion MotionOf(Entity& entity, bool isTrigger)
        {
            if (isTrigger)
            {
                return Backend::Motion{.motionType = MotionType::Kinematic};
            }
            return MotionOf(RigidBodyOf(entity));
        }

        // The rigid body entity a collider belongs to: its own, until a body can have colliders on several entities.
        std::uint32_t BodyOf(std::uint32_t collider)
        {
            return collider;
        }

        template <typename TEvent, typename Key>
        void SortAndSend(App& app, std::vector<TEvent>& events, Key key)
        {
            std::sort(events.begin(), events.end(),
                      [&key](const TEvent& a, const TEvent& b) { return key(a) < key(b); });
            for (const TEvent& event : events)
            {
                app.SendEvent(event);
            }
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

    void PhysicsWorld::AddImpulse(const Entity& entity, const Math::Vec3& impulse)
    {
        m_impl->Queue(entity.GetId(), &Backend::World::AddImpulse, impulse);
    }

    void PhysicsWorld::AddForce(const Entity& entity, const Math::Vec3& force)
    {
        m_impl->Queue(entity.GetId(), &Backend::World::AddForce, force);
    }

    void PhysicsWorld::AddTorque(const Entity& entity, const Math::Vec3& torque)
    {
        m_impl->Queue(entity.GetId(), &Backend::World::AddTorque, torque);
    }

    void PhysicsWorld::AddAngularImpulse(const Entity& entity, const Math::Vec3& impulse)
    {
        m_impl->Queue(entity.GetId(), &Backend::World::AddAngularImpulse, impulse);
    }

    Math::Vec3 PhysicsWorld::Gravity() const
    {
        return m_impl->Gravity();
    }

    void PhysicsWorld::SetGravity(const Math::Vec3& gravity)
    {
        m_impl->SetGravity(gravity);
    }

    PhysicsWorld::Impl::Impl(EntityList& entities, const Backend::WorldSettings& settings,
                             Backend::CollisionMeshes& meshes, ILogger& logger)
        : m_entities(&entities), m_logger(&logger), m_backend(settings, meshes, logger),
          m_rigidBodies(entities.Watch(ComponentType::Native<RigidBody>())),
          m_colliders(entities.Watch(ComponentType::Native<Collider>())),
          m_triggers(entities.Watch(ComponentType::Native<Trigger>()))
    {
        // A watch records only what happens after it is made: the colliders already there are found once, here.
        const ComponentType colliders[] = {ComponentType::Native<Collider>()};
        entities.ForEachWith(colliders, [this](std::uint32_t entity) { m_pending.insert(entity); });
    }

    void PhysicsWorld::ForEachContact(const Entity& body, const std::function<void(const Contact&)>& func) const
    {
        m_impl->ForEachContact(body, func);
    }

    void PhysicsWorld::Impl::Step(App& app, float dt)
    {
        SyncBodies();
        PushEdits(dt);
        ApplyPushes();
        m_backend.Step(dt, CollisionSteps(dt));
        WriteBack();
        SendEvents(app);
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
            if (!record.Active() || record.motion.motionType == MotionType::Static || record.heldByGame ||
                !m_entities->HasEntity(id))
            {
                continue; // an inactive collider has no simulated pose to show
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
        // Becoming or ceasing to be a trigger changes what kind of backend body the collider needs.
        for (const ComponentChange& change : m_triggers.Drain())
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
            const bool isTrigger = entity.HasComponent<Trigger>();
            const RigidBody* rigidBody = isTrigger ? nullptr : RigidBodyOf(entity);
            const Transform world = GetWorldTransform(entity);
            const CollisionShape& shape = entity.GetComponent<Collider>().shape;

            Backend::BodyDesc desc;
            desc.motion = MotionOf(entity, isTrigger);
            desc.canMove = isTrigger || rigidBody != nullptr;
            desc.isTrigger = isTrigger;
            desc.shape = shape;
            desc.material = MaterialOf(entity);
            desc.filter = EffectiveFilterOf(entity);
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
                m_bodies.emplace(id, Record{Backend::BodyId{}, desc.canMove, desc.motion, desc.shape, desc.material,
                                            desc.filter, Transform(desc.position, desc.rotation, desc.scale),
                                            desc.linearVelocity, desc.angularVelocity, desc.isTrigger});
                continue;
            }
            entities.push_back(id);
            descs.push_back(desc);
        }

        std::vector<Backend::BodyId> ids(descs.size());
        m_backend.CreateBodies(descs, ids);
        for (std::size_t i = 0; i < descs.size(); ++i)
        {
            // A body the backend could not make (the world is full; it logged that) leaves the record inactive.
            const Backend::BodyDesc& desc = descs[i];
            m_bodies.emplace(entities[i], Record{ids[i], desc.canMove, desc.motion, desc.shape, desc.material,
                                                 desc.filter, Transform(desc.position, desc.rotation, desc.scale),
                                                 desc.linearVelocity, desc.angularVelocity, desc.isTrigger});
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
            if (record.Active() ? !m_backend.CanMakeShape(shape) : !Same(shape, record.shape))
            {
                changed.insert(id);
            }
        }
    }

    void PhysicsWorld::Impl::PushEdits(float dt)
    {
        for (auto& [id, record] : m_bodies)
        {
            if (!record.Active())
            {
                continue; // inactive: no body to push to
            }
            Entity entity = m_entities->GetEntity(id);
            RigidBody* rigidBody = record.isTrigger ? nullptr : RigidBodyOf(entity);
            const Backend::Motion motion = MotionOf(entity, record.isTrigger);
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

            const CollisionFilter filter = EffectiveFilterOf(entity);
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

    void PhysicsWorld::Impl::Queue(std::uint32_t entity, Pusher pusher, const Math::Vec3& amount)
    {
        m_pushes.push_back(Push{entity, pusher, amount});
    }

    void PhysicsWorld::Impl::ApplyPushes()
    {
        for (const Push& push : m_pushes)
        {
            const auto it = m_bodies.find(push.entity);
            if (it == m_bodies.end() || !it->second.Active())
            {
                continue; // no body: nothing to push
            }
            (m_backend.*push.pusher)(it->second.id, push.amount);
        }
        m_pushes.clear();
    }

    void PhysicsWorld::Impl::WriteBack()
    {
        for (auto& [id, record] : m_bodies)
        {
            if (record.motion.motionType != MotionType::Dynamic || !record.Active())
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

    CollisionFilter PhysicsWorld::Impl::EffectiveFilterOf(Entity& entity) const
    {
        if (entity.HasComponent<Trigger>() && !entity.GetComponent<Trigger>().enabled)
        {
            return CollisionFilter{.layers = 0u, .mask = 0u};
        }
        return FilterOf(entity);
    }

    bool PhysicsWorld::Impl::ReportsContacts(std::uint32_t body) const
    {
        if (!m_entities->HasEntity(body))
        {
            return false;
        }
        Entity entity = m_entities->GetEntity(body);
        return entity.HasComponent<ContactEvents>() && entity.GetComponent<ContactEvents>().enabled;
    }

    void PhysicsWorld::Impl::SendEvents(App& app)
    {
        std::vector<ContactBegin> begins;
        std::vector<ContactEnd> ends;
        std::vector<TriggerEnter> enters;
        std::vector<TriggerExit> exits;
        const auto entity = [this](std::uint32_t id) { return m_entities->HandleOf(id); };

        for (const Backend::TouchChange& change : m_backend.TouchChanges())
        {
            const Backend::Touch& touch = change.touch;
            const bool isBegin = change.kind == Backend::TouchChange::Kind::Begin;
            if (touch.isTrigger1 || touch.isTrigger2)
            {
                const bool firstIsTrigger = touch.isTrigger1;
                const std::uint32_t other = firstIsTrigger ? touch.entity2 : touch.entity1;
                const OverlapKey key{firstIsTrigger ? touch.entity1 : touch.entity2, BodyOf(other), other};
                if (isBegin && m_reportedOverlaps.insert(key).second)
                {
                    enters.push_back(
                        TriggerEnter{entity(key.trigger), entity(key.otherBody), entity(key.otherCollider)});
                }
                else if (!isBegin && m_reportedOverlaps.erase(key) > 0)
                {
                    exits.push_back(TriggerExit{entity(key.trigger), entity(key.otherBody), entity(key.otherCollider)});
                }
                continue;
            }

            // A and B in entity order, so the order does not depend on the backend's body ids.
            const std::uint32_t body1 = BodyOf(touch.entity1);
            const std::uint32_t body2 = BodyOf(touch.entity2);
            const bool swap = std::tie(body2, touch.entity2) < std::tie(body1, touch.entity1);
            const ContactKey key = swap ? ContactKey{body2, body1, touch.entity2, touch.entity1}
                                        : ContactKey{body1, body2, touch.entity1, touch.entity2};
            if (isBegin)
            {
                if ((ReportsContacts(key.bodyA) || ReportsContacts(key.bodyB)) && m_reportedContacts.insert(key).second)
                {
                    begins.push_back(ContactBegin{entity(key.bodyA), entity(key.bodyB), entity(key.colliderA),
                                                  entity(key.colliderB), touch.point,
                                                  swap ? -touch.normal : touch.normal, touch.relativeSpeed});
                }
            }
            else if (m_reportedContacts.erase(key) > 0)
            {
                ends.push_back(
                    ContactEnd{entity(key.bodyA), entity(key.bodyB), entity(key.colliderA), entity(key.colliderB)});
            }
        }

        // A contact neither body reports any more (ContactEvents removed or disabled) ends now, as if it separated.
        for (auto it = m_reportedContacts.begin(); it != m_reportedContacts.end();)
        {
            if (ReportsContacts(it->bodyA) || ReportsContacts(it->bodyB))
            {
                ++it;
                continue;
            }
            ends.push_back(
                ContactEnd{entity(it->bodyA), entity(it->bodyB), entity(it->colliderA), entity(it->colliderB)});
            it = m_reportedContacts.erase(it);
        }

        // Sorted by entity, so the order is the same on every run whatever order the backend found them in.
        const auto contactOrder = [](const auto& event) {
            return std::make_tuple(event.bodyA.GetId(), event.bodyB.GetId(), event.colliderA.GetId(),
                                   event.colliderB.GetId());
        };
        const auto overlapOrder = [](const auto& event) {
            return std::make_tuple(event.trigger.GetId(), event.otherBody.GetId(), event.otherCollider.GetId());
        };
        SortAndSend(app, begins, contactOrder);
        SortAndSend(app, ends, contactOrder);
        SortAndSend(app, enters, overlapOrder);
        SortAndSend(app, exits, overlapOrder);
    }

    void PhysicsWorld::Impl::ForEachContact(const Entity& body, const std::function<void(const Contact&)>& func) const
    {
        const std::uint32_t asked = body.GetId();
        for (const Backend::Touch& touch : m_backend.Touches())
        {
            if (touch.isTrigger1 || touch.isTrigger2)
            {
                continue;
            }
            const auto entity = [this](std::uint32_t id) { return m_entities->HandleOf(id); };
            if (BodyOf(touch.entity1) == asked)
            {
                func(Contact{entity(BodyOf(touch.entity1)), entity(touch.entity1), entity(BodyOf(touch.entity2)),
                             entity(touch.entity2), touch.point, touch.normal});
            }
            else if (BodyOf(touch.entity2) == asked)
            {
                func(Contact{entity(BodyOf(touch.entity2)), entity(touch.entity2), entity(BodyOf(touch.entity1)),
                             entity(touch.entity1), touch.point, -touch.normal});
            }
        }
    }
} // namespace Fenrir
