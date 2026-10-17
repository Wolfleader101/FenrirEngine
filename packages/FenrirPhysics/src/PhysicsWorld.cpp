#include "FenrirPhysics/PhysicsWorld.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include "BodyComponents.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirPhysics/Events.hpp"
#include "FenrirScene/Scene.hpp"
#include "Same.hpp"
#include "WorldImpl.hpp"

namespace Fenrir
{
    namespace
    {
        // The diff's overloads below would otherwise hide those for plain values.
        using Fenrir::Same;

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

        Entity ParentOf(const Entity& entity)
        {
            return entity.HasComponent<Relationship>() ? entity.GetComponent<Relationship>().parent : Entity();
        }

        Transform LocalOf(const Entity& entity)
        {
            return entity.HasComponent<Transform>() ? entity.GetComponent<Transform>() : Transform();
        }

        // The backend keeps one surface per body, so a compound body has one material: its own entity's, else that of
        // the first of its colliders (in entity order) with one, else the default.
        template <typename Parts>
        PhysicsMaterial MaterialOf(EntityList& entities, const Entity& body, const Parts& parts)
        {
            if (body.HasComponent<PhysicsMaterial>())
            {
                return body.GetComponent<PhysicsMaterial>();
            }
            for (const auto& part : parts)
            {
                const Entity collider = entities.GetEntity(part.entity);
                if (collider.HasComponent<PhysicsMaterial>())
                {
                    return collider.GetComponent<PhysicsMaterial>();
                }
            }
            return PhysicsMaterial{};
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

        // A trigger collider is always its own backend body (the backend's sensor flag is per body), even beneath
        // another body.
        bool IsTrigger(const Entity& entity)
        {
            return entity.HasComponent<Trigger>() && entity.HasComponent<Collider>();
        }

        // The nearest of `from` and its ancestors with a RigidBody that hosts a compound body, or none. A trigger's
        // entity hosts no compound body: a RigidBody there does not make the trigger move as one.
        Entity NearestBodyHost(const Entity& from)
        {
            for (Entity e = from; e.IsValid(); e = ParentOf(e))
            {
                if (e.HasComponent<RigidBody>() && !e.HasComponent<Trigger>())
                {
                    return e;
                }
            }
            return Entity();
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

    PhysicsWorld::PhysicsWorld(Key /*key*/, std::unique_ptr<Impl> impl) : m_impl(std::move(impl))
    {
    }

    std::size_t PhysicsWorld::DistinctShapeCount() const
    {
        return m_impl->m_backend.DistinctShapeCount();
    }

    PhysicsWorld::~PhysicsWorld() = default;

    PhysicsWorld* PhysicsWorld::Of(Scene& scene)
    {
        return scene.GetResource<PhysicsWorld>();
    }

    void PhysicsWorld::AddImpulse(const Entity& entity, const Math::Vec3& impulse)
    {
        m_impl->Queue(entity, &Backend::World::AddImpulse, impulse);
    }

    void PhysicsWorld::AddForce(const Entity& entity, const Math::Vec3& force)
    {
        m_impl->Queue(entity, &Backend::World::AddForce, force);
    }

    void PhysicsWorld::AddTorque(const Entity& entity, const Math::Vec3& torque)
    {
        m_impl->Queue(entity, &Backend::World::AddTorque, torque);
    }

    void PhysicsWorld::AddAngularImpulse(const Entity& entity, const Math::Vec3& impulse)
    {
        m_impl->Queue(entity, &Backend::World::AddAngularImpulse, impulse);
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
          m_triggers(entities.Watch(ComponentType::Native<Trigger>())),
          m_jointWatch(entities.Watch(ComponentType::Native<Joint>()))
    {
        // A watch records only what happens after it is made: the components already there are found once, here.
        const ComponentType colliders[] = {ComponentType::Native<Collider>()};
        entities.ForEachWith(colliders, [this](std::uint32_t entity) { m_colliderEntities.insert(entity); });
        const ComponentType rigidBodies[] = {ComponentType::Native<RigidBody>()};
        entities.ForEachWith(rigidBodies, [this](std::uint32_t entity) { m_rigidBodyEntities.insert(entity); });
        const ComponentType joints[] = {ComponentType::Native<Joint>()};
        entities.ForEachWith(joints, [this](std::uint32_t entity) { m_jointEntities.insert(entity); });
    }

    void PhysicsWorld::ForEachContact(const Entity& entity, const std::function<void(const Contact&)>& func) const
    {
        m_impl->ForEachContact(entity, func);
    }

    void PhysicsWorld::Impl::Step(App& app, float dt)
    {
        SyncBodies();
        PushEdits(dt);
        // After the bodies' edits: a body's scale decides whether its joints are recreated.
        SyncJoints(dt);
        ApplyPushes();
        m_backend.Step(dt, CollisionSteps(dt));
        WriteBack();
        MeasureJoints();
        BreakJoints();
        SendEvents(app);
        AdvancePoses();
        DrawDebugLines();
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
            // Shown at its step pose: a body that is not interpolated, written only because an ancestor is.
            bool atStepPose;
        };
        std::vector<Blended> blended;
        std::vector<Blended> carried;
        std::set<std::uint32_t> interpolated;
        for (auto& [id, record] : m_bodies)
        {
            if (!record.Active() || record.motion.motionType == MotionType::Static || record.heldByGame ||
                !m_entities->HasEntity(id))
            {
                continue; // an inactive collider has no simulated pose to show
            }
            Entity entity = m_entities->GetEntity(id);
            const RigidBody* rigidBody = RigidBodyOf(entity);
            if (rigidBody == nullptr || !entity.HasComponent<Transform>())
            {
                continue;
            }
            if (rigidBody->interpolate)
            {
                interpolated.insert(id);
                blended.push_back(Blended{DepthOf(entity), entity, &record, false});
            }
            else
            {
                carried.push_back(Blended{DepthOf(entity), entity, &record, true});
            }
        }
        // A body that is not interpolated is still moved by an interpolated ancestor's blend, through its local
        // Transform: it is written back at its own step pose, so it is shown where it is.
        for (const Blended& body : carried)
        {
            for (Entity above = ParentOf(body.entity); above.IsValid(); above = ParentOf(above))
            {
                if (interpolated.contains(above.GetId()))
                {
                    blended.push_back(body);
                    break;
                }
            }
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
            if (!body.atStepPose)
            {
                world.pos = record.previous.pos + (record.current.pos - record.previous.pos) * fraction;
                world.rot = Math::Slerp(record.previous.rot, record.current.rot, fraction);
            }
            SetWorldTransform(body.entity, world);
            record.shownLocal = body.entity.GetComponent<Transform>();
            record.blended = true;
        }
    }

    std::uint32_t PhysicsWorld::Impl::BodyEntityOf(std::uint32_t entity) const
    {
        const Entity collider = m_entities->GetEntity(entity);
        if (IsTrigger(collider))
        {
            return entity;
        }
        const Entity host = NearestBodyHost(collider);
        return host.IsValid() ? host.GetId() : entity;
    }

    std::uint32_t PhysicsWorld::Impl::ResolveBody(std::uint32_t entity) const
    {
        if (!m_entities->HasEntity(entity))
        {
            return entity;
        }
        return HasBodyComponent(m_entities->GetEntity(entity)) ? BodyEntityOf(entity) : entity;
    }

    std::uint32_t PhysicsWorld::Impl::AttachedBodyOf(std::uint32_t trigger) const
    {
        if (!m_entities->HasEntity(trigger))
        {
            return trigger;
        }
        // The body a collider on the parent would belong to: the nearest ancestor body, if there is one.
        const Entity host = NearestBodyHost(ParentOf(m_entities->GetEntity(trigger)));
        return host.IsValid() ? host.GetId() : trigger;
    }

    PhysicsWorld::Impl::Record* PhysicsWorld::Impl::FindBody(std::uint32_t entity)
    {
        if (m_entities->HasEntity(entity) && !HasBodyComponent(m_entities->GetEntity(entity)))
        {
            return nullptr;
        }
        // A destroyed entity's body, if it had one, lasts until the next step.
        const auto it = m_bodies.find(ResolveBody(entity));
        return it == m_bodies.end() ? nullptr : &it->second;
    }

    PhysicsWorld::Impl::Record* PhysicsWorld::Impl::FindBody(const Entity& entity)
    {
        return entity.IsValid() ? FindBody(entity.GetId()) : nullptr;
    }

    std::vector<PhysicsWorld::Impl::Part> PhysicsWorld::Impl::PartsOf(std::span<const std::uint32_t> entities,
                                                                      std::uint32_t body, const Math::Vec3& bodyScale)
    {
        std::vector<Part> parts;
        parts.reserve(entities.size());
        std::vector<Entity> chain;
        for (std::uint32_t id : entities)
        {
            Entity entity = m_entities->GetEntity(id);
            // Composed from the body down through the locals, with GetWorldTransform's own step, but starting from the
            // body's scale alone: the pose then stays the same, bit for bit, however the body moves.
            chain.clear();
            for (Entity e = entity; e.IsValid() && e.GetId() != body; e = ParentOf(e))
            {
                chain.push_back(e);
            }
            Transform pose(Math::Vec3(0.0f), Math::Quat(1.0f, 0.0f, 0.0f, 0.0f), bodyScale);
            for (auto it = chain.rbegin(); it != chain.rend(); ++it)
            {
                pose = ComposeTransform(pose, LocalOf(*it));
            }
            const CollisionShape& shape = entity.GetComponent<Collider>().shape;
            parts.push_back(Part{id, shape, pose, m_backend.CanMakeShape(shape),
                                 id != body && entity.HasComponent<CollisionFilter>()});
        }
        return parts;
    }

    std::vector<Backend::ColliderPart> PhysicsWorld::Impl::UsableParts(std::span<const Part> parts)
    {
        std::vector<Backend::ColliderPart> usable;
        for (const Part& part : parts)
        {
            if (part.usable)
            {
                usable.push_back(
                    Backend::ColliderPart{part.shape, part.pose.scale, part.pose.pos, part.pose.rot, part.entity});
            }
        }
        return usable;
    }

    void PhysicsWorld::Impl::WarnAbout(std::span<const Part> parts, std::span<const Part> before)
    {
        for (std::size_t i = 0; i < parts.size(); ++i)
        {
            const Part& part = parts[i];
            const bool known = i < before.size() && before[i].entity == part.entity;
            if (!part.usable && (!known || before[i].usable))
            {
                m_logger->Warn("The collider of entity " + std::to_string(part.entity) +
                               " refers to a collision mesh that is released or cannot be used as its kind of shape; "
                               "it is inactive until its shape changes");
            }
            if (part.filtered && (!known || !before[i].filtered))
            {
                m_logger->Warn("The CollisionFilter of entity " + std::to_string(part.entity) +
                               " is ignored: its collider is part of the body of an ancestor, whose filter applies to "
                               "all its colliders");
            }
        }
    }

    void PhysicsWorld::Impl::SyncBodies()
    {
        // Only each entity's net effect matters: an entity whose components came or went has its body rebuilt from
        // what it has now (added then removed: no body; removed then added: a new body).
        std::set<std::uint32_t> changed;
        const auto track = [this, &changed](ComponentWatch& watch, std::set<std::uint32_t>& tracked, auto has) {
            for (const ComponentChange& change : watch.Drain())
            {
                changed.insert(change.entity);
                if (m_entities->HasEntity(change.entity) && has(m_entities->GetEntity(change.entity)))
                {
                    tracked.insert(change.entity);
                }
                else
                {
                    tracked.erase(change.entity);
                }
            }
        };
        track(m_rigidBodies, m_rigidBodyEntities, [](const Entity& e) { return e.HasComponent<RigidBody>(); });
        track(m_colliders, m_colliderEntities, [](const Entity& e) { return e.HasComponent<Collider>(); });
        // Becoming or ceasing to be a trigger changes what kind of backend body the collider needs, and which body it
        // is part of.
        for (const ComponentChange& change : m_triggers.Drain())
        {
            changed.insert(change.entity);
        }
        DrainJoints();

        // Which colliders form which body, from the hierarchy as it is now (reparenting is not reported, so this is
        // worked out every tick).
        std::map<std::uint32_t, std::vector<std::uint32_t>> assembly;
        for (std::uint32_t collider : m_colliderEntities)
        {
            assembly[BodyEntityOf(collider)].push_back(collider);
        }

        for (std::uint32_t rigidBody : m_rigidBodyEntities)
        {
            if (assembly.contains(rigidBody))
            {
                m_empty.erase(rigidBody);
            }
            else if (m_empty.insert(rigidBody).second)
            {
                m_logger->Warn("The RigidBody of entity " + std::to_string(rigidBody) +
                               " has no collider on its entity or beneath it; it is inactive in physics until it has "
                               "one");
            }
        }
        std::erase_if(m_empty, [this](std::uint32_t entity) { return !m_rigidBodyEntities.contains(entity); });

        // Rebuilt: bodies whose entity or colliders changed, whose set of colliders changed, or whose colliders'
        // shapes became usable or unusable (a collision mesh released, or a shape changed to one that can be made).
        std::vector<Backend::BodyId> destroyed;
        for (auto it = m_bodies.begin(); it != m_bodies.end();)
        {
            const auto& [body, record] = *it;
            const auto wanted = assembly.find(body);
            bool rebuild =
                wanted == assembly.end() || changed.contains(body) || wanted->second.size() != record.parts.size();
            for (std::size_t i = 0; !rebuild && i < record.parts.size(); ++i)
            {
                const std::uint32_t collider = wanted->second[i];
                rebuild = collider != record.parts[i].entity || changed.contains(collider) ||
                          m_backend.CanMakeShape(m_entities->GetEntity(collider).GetComponent<Collider>().shape) !=
                              record.parts[i].usable;
            }
            if (!rebuild)
            {
                assembly.erase(wanted);
                ++it;
                continue;
            }
            destroyed.push_back(record.id);
            it = m_bodies.erase(it);
        }
        RemoveJointsOf(destroyed);
        m_backend.DestroyBodies(destroyed);

        std::vector<std::uint32_t> entities;
        std::vector<Backend::BodyDesc> descs;
        for (const auto& [id, colliders] : assembly)
        {
            Entity entity = m_entities->GetEntity(id);
            const bool isTrigger = IsTrigger(entity);
            const RigidBody* rigidBody = isTrigger ? nullptr : RigidBodyOf(entity);
            const Transform world = GetWorldTransform(entity);
            std::vector<Part> parts = PartsOf(colliders, id, world.scale);
            WarnAbout(parts, {});

            Backend::BodyDesc desc;
            desc.motion = MotionOf(entity, isTrigger);
            desc.canMove = isTrigger || rigidBody != nullptr;
            desc.isTrigger = isTrigger;
            desc.parts = UsableParts(parts);
            desc.material = MaterialOf(*m_entities, entity, parts);
            desc.filter = EffectiveFilterOf(entity);
            desc.position = world.pos;
            desc.rotation = world.rot;
            if (rigidBody != nullptr)
            {
                desc.linearVelocity = rigidBody->linearVelocity;
                desc.angularVelocity = rigidBody->angularVelocity;
            }
            desc.entity = id;
            const auto made = m_bodies.emplace(id, Record{Backend::BodyId{}, desc.canMove, desc.motion,
                                                          std::move(parts), desc.material, desc.filter, world,
                                                          desc.linearVelocity, desc.angularVelocity});
            made.first->second.isTrigger = isTrigger;
            if (desc.parts.empty())
            {
                continue; // inactive: none of its colliders can be made
            }
            entities.push_back(id);
            descs.push_back(std::move(desc));
        }

        std::vector<Backend::BodyId> ids(descs.size());
        m_backend.CreateBodies(descs, ids);
        for (std::size_t i = 0; i < descs.size(); ++i)
        {
            m_bodies.at(entities[i]).id = ids[i];
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

            std::vector<std::uint32_t> colliders;
            colliders.reserve(record.parts.size());
            for (const Part& part : record.parts)
            {
                colliders.push_back(part.entity);
            }
            std::vector<Part> parts = PartsOf(colliders, id, world.scale);
            WarnAbout(parts, record.parts);
            if (!std::ranges::equal(parts, record.parts, [](const Part& a, const Part& b) {
                    return Same(a.shape, b.shape) && Same(a.pose.pos, b.pose.pos) && Same(a.pose.rot, b.pose.rot) &&
                           Same(a.pose.scale, b.pose.scale);
                }))
            {
                m_backend.SetShape(record.id, UsableParts(parts), id, motion.mass);
            }
            record.parts = std::move(parts);

            const PhysicsMaterial material = MaterialOf(*m_entities, entity, record.parts);
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

    void PhysicsWorld::Impl::Queue(const Entity& entity, Pusher pusher, const Math::Vec3& amount)
    {
        if (entity.IsValid())
        {
            m_pushes.push_back(Push{entity.GetId(), pusher, amount});
        }
    }

    void PhysicsWorld::Impl::ApplyPushes()
    {
        for (const Push& push : m_pushes)
        {
            const Record* record = FindBody(push.entity);
            if (record == nullptr || !record->Active())
            {
                continue; // no body: nothing to push
            }
            (m_backend.*push.pusher)(record->id, push.amount);
        }
        m_pushes.clear();
    }

    void PhysicsWorld::Impl::WriteBack()
    {
        // Parents before children: a body's local Transform is written against its parent's world transform, so a
        // parent written after it would move it again.
        std::vector<std::pair<int, std::uint32_t>> order;
        for (const auto& [id, record] : m_bodies)
        {
            if (record.motion.motionType == MotionType::Dynamic && record.Active())
            {
                order.emplace_back(DepthOf(m_entities->GetEntity(id)), id);
            }
        }
        std::ranges::sort(order);

        for (const auto& [depth, id] : order)
        {
            Record& record = m_bodies.at(id);
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
                const OverlapKey key{firstIsTrigger ? touch.entity1 : touch.entity2,
                                     firstIsTrigger ? touch.entity2 : touch.entity1,
                                     firstIsTrigger ? touch.collider2 : touch.collider1};
                // A trigger beneath a body ignores that body: it moves with it, so it would always be inside it.
                if (isBegin && key.otherBody == AttachedBodyOf(key.trigger))
                {
                    continue;
                }
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
            const bool swap = std::tie(touch.entity2, touch.collider2) < std::tie(touch.entity1, touch.collider1);
            const ContactKey key = swap ? ContactKey{touch.entity2, touch.entity1, touch.collider2, touch.collider1}
                                        : ContactKey{touch.entity1, touch.entity2, touch.collider1, touch.collider2};
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
        SortAndSend(app, m_broken, [](const JointBroken& event) { return event.joint.GetId(); });
        m_broken.clear();
    }

    void PhysicsWorld::Impl::ForEachContact(const Entity& entity, const std::function<void(const Contact&)>& func) const
    {
        if (!entity.IsValid())
        {
            return; // a destroyed entity's touches end at the next step, but it has no body to ask about now
        }
        // A child collider's entity stands for the compound body it belongs to.
        const std::uint32_t asked = ResolveBody(entity.GetId());
        for (const Backend::Touch& touch : m_backend.Touches())
        {
            if (touch.isTrigger1 || touch.isTrigger2)
            {
                continue;
            }
            const auto handle = [this](std::uint32_t id) { return m_entities->HandleOf(id); };
            if (touch.entity1 == asked)
            {
                func(Contact{handle(touch.entity1), handle(touch.collider1), handle(touch.entity2),
                             handle(touch.collider2), touch.point, touch.normal});
            }
            else if (touch.entity2 == asked)
            {
                func(Contact{handle(touch.entity2), handle(touch.collider2), handle(touch.entity1),
                             handle(touch.collider1), touch.point, -touch.normal});
            }
        }
    }
} // namespace Fenrir
