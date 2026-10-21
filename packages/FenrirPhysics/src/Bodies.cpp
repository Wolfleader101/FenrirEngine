#include "Bodies.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "BodyComponents.hpp"
#include "DebugCanvas.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FilterOf.hpp"
#include "Hierarchy.hpp"
#include "Poses.hpp"
#include "Same.hpp"

namespace Fenrir::Detail
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

        // The filter a body's backend body has, read from the body's entity: it applies to all the body's colliders. A
        // disabled trigger's collides with nothing.
        CollisionFilter EffectiveFilterOf(const Entity& entity)
        {
            if (entity.HasComponent<Trigger>() && !entity.GetComponent<Trigger>().enabled)
            {
                return CollisionFilter{.layers = 0u, .mask = 0u};
            }
            return FilterOf(entity);
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

        // How Poses takes a made body of a motion type: a dynamic one is written back, a kinematic one followed, and a
        // static one still.
        Poses::Kind PoseKindOf(MotionType motionType)
        {
            if (motionType == MotionType::Dynamic)
            {
                return Poses::Kind::Written;
            }
            return motionType == MotionType::Kinematic ? Poses::Kind::Followed : Poses::Kind::Still;
        }

        // The nearest of `from` and its ancestors that hosts a compound body, or none.
        Entity NearestBodyHost(const Entity& from)
        {
            for (Entity e = from; e.IsValid(); e = ParentOf(e))
            {
                if (IsBodyHost(e))
                {
                    return e;
                }
            }
            return Entity();
        }
    } // namespace

    bool IsCharacterCollider(const Entity& entity)
    {
        // Beside a RigidBody the RigidBody works instead, and the Collider is its.
        return entity.HasComponent<CharacterBody>() && !entity.HasComponent<RigidBody>();
    }

    bool IsCharacterChild(const Entity& collider)
    {
        // A trigger is its own body beneath anything.
        if (collider.HasComponent<RigidBody>() || collider.HasComponent<Trigger>() || IsCharacterCollider(collider))
        {
            return false;
        }
        // The nearest ancestor that is a body host or a character decides: a RigidBody's compound, or no body at all.
        for (Entity above = ParentOf(collider); above.IsValid(); above = ParentOf(above))
        {
            if (IsCharacterCollider(above))
            {
                return true;
            }
            if (IsBodyHost(above))
            {
                return false;
            }
        }
        return false;
    }

    Bodies::Bodies(EntityList& entities, ILogger& logger, Backend::World& backend, Poses& poses)
        : m_entities(&entities), m_logger(&logger), m_backend(&backend), m_poses(&poses),
          m_rigidBodyEntities(entities, ComponentType::Native<RigidBody>()),
          m_colliderEntities(entities, ComponentType::Native<Collider>()),
          m_characterEntities(entities, ComponentType::Native<CharacterBody>()),
          m_triggerEntities(entities, ComponentType::Native<Trigger>())
    {
    }

    std::uint32_t Bodies::BodyEntityOf(std::uint32_t entity) const
    {
        const Entity collider = m_entities->GetEntity(entity);
        if (IsTrigger(collider))
        {
            return entity;
        }
        const Entity host = NearestBodyHost(collider);
        return host.IsValid() ? host.GetId() : entity;
    }

    std::uint32_t Bodies::ResolveBody(std::uint32_t entity) const
    {
        if (!m_entities->HasEntity(entity))
        {
            return entity;
        }
        return HasBodyComponent(m_entities->GetEntity(entity)) ? BodyEntityOf(entity) : entity;
    }

    std::uint32_t Bodies::AttachedBodyOf(std::uint32_t trigger) const
    {
        if (!m_entities->HasEntity(trigger))
        {
            return trigger;
        }
        // The nearest ancestor body or character, if there is one: a trigger beneath a character moves with it too.
        for (Entity above = ParentOf(m_entities->GetEntity(trigger)); above.IsValid(); above = ParentOf(above))
        {
            if (IsCharacterCollider(above) || IsBodyHost(above))
            {
                return above.GetId();
            }
        }
        return trigger;
    }

    const Bodies::Body* Bodies::BodyOf(std::uint32_t entity) const
    {
        const auto it = m_bodies.find(entity);
        return it == m_bodies.end() ? nullptr : &it->second;
    }

    const Bodies::Body* Bodies::FindBody(std::uint32_t entity) const
    {
        if (m_entities->HasEntity(entity) && !HasBodyComponent(m_entities->GetEntity(entity)))
        {
            return nullptr;
        }
        // A destroyed entity's body, if it had one, lasts until the next step.
        return BodyOf(ResolveBody(entity));
    }

    const Bodies::Body* Bodies::FindBody(const Entity& entity) const
    {
        return entity.IsValid() ? FindBody(entity.GetId()) : nullptr;
    }

    std::vector<Bodies::Part> Bodies::PartsOf(std::span<const std::uint32_t> entities, std::uint32_t body,
                                              const Math::Vec3& bodyScale) const
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
            parts.push_back(Part{id, shape, pose, m_backend->CanMakeShape(shape),
                                 id != body && entity.HasComponent<CollisionFilter>()});
        }
        return parts;
    }

    std::vector<Backend::ColliderPart> Bodies::UsableParts(std::span<const Part> parts)
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

    void Bodies::WarnAbout(std::span<const Part> parts, std::span<const Part> before)
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

    Bodies::Rebuild Bodies::Plan()
    {
        // Only each entity's net effect matters: an entity whose components came or went has its body rebuilt from
        // what it has now (added then removed: no body; removed then added: a new body).
        Rebuild rebuild;
        const auto track = [&rebuild](TrackedComponents& tracked) {
            for (const std::uint32_t entity : tracked.Drain())
            {
                rebuild.changed.insert(entity);
            }
        };
        track(m_rigidBodyEntities);
        track(m_colliderEntities);
        // Becoming or ceasing to be a character body changes whether the entity's collider is a body of its own.
        track(m_characterEntities);
        // Becoming or ceasing to be a trigger changes what kind of backend body the collider needs, and which body it
        // is part of.
        track(m_triggerEntities);

        // Which colliders form which body, from the hierarchy as it is now (reparenting is not reported, so this is
        // worked out every tick).
        std::map<std::uint32_t, std::vector<std::uint32_t>>& assembly = m_toMake;
        assembly.clear();
        std::erase_if(m_characterChildren, [this](std::uint32_t id) { return !m_colliderEntities.Contains(id); });
        for (std::uint32_t collider : m_colliderEntities.Entities())
        {
            const Entity entity = m_entities->GetEntity(collider);
            if (IsCharacterCollider(entity))
            {
                continue;
            }
            if (!IsCharacterChild(entity))
            {
                m_characterChildren.erase(collider);
                assembly[BodyEntityOf(collider)].push_back(collider);
            }
            else if (m_characterChildren.insert(collider).second)
            {
                m_logger->Warn("The collider of entity " + std::to_string(collider) +
                               " is beneath a character body, which has no compound shape; it makes no body until it "
                               "has a RigidBody of its own or is moved from beneath the character");
            }
        }

        for (std::uint32_t rigidBody : m_rigidBodyEntities.Entities())
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
        std::erase_if(m_empty, [this](std::uint32_t entity) { return !m_rigidBodyEntities.Contains(entity); });

        // Rebuilt: bodies whose entity or colliders changed, whose set of colliders changed, or whose colliders'
        // shapes became usable or unusable (a collision mesh released, or a shape changed to one that can be made).
        // What is kept is left out of the assembly, which leaves the bodies to make.
        const std::set<std::uint32_t>& changed = rebuild.changed;
        for (auto it = m_bodies.begin(); it != m_bodies.end();)
        {
            const auto& [body, record] = *it;
            const auto wanted = assembly.find(body);
            bool remake =
                wanted == assembly.end() || changed.contains(body) || wanted->second.size() != record.parts.size();
            for (std::size_t i = 0; !remake && i < record.parts.size(); ++i)
            {
                const std::uint32_t collider = wanted->second[i];
                remake = collider != record.parts[i].entity || changed.contains(collider) ||
                         m_backend->CanMakeShape(m_entities->GetEntity(collider).GetComponent<Collider>().shape) !=
                             record.parts[i].usable;
            }
            if (!remake)
            {
                assembly.erase(wanted);
                ++it;
                continue;
            }
            rebuild.destroyed.push_back(record.id);
            m_poses->Forget(body);
            it = m_bodies.erase(it);
        }
        return rebuild;
    }

    void Bodies::Apply(const Rebuild& rebuild)
    {
        m_backend->DestroyBodies(rebuild.destroyed);

        std::vector<std::uint32_t> entities;
        std::vector<Backend::BodyDesc> descs;
        for (const auto& [id, colliders] : m_toMake)
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
            m_bodies.emplace(id, Record{{Backend::BodyId{}, desc.motion, isTrigger},
                                        desc.canMove,
                                        std::move(parts),
                                        desc.material,
                                        desc.filter,
                                        desc.linearVelocity,
                                        desc.angularVelocity});
            m_poses->Made(id, world);
            if (desc.parts.empty())
            {
                continue; // inactive: none of its colliders can be made
            }
            entities.push_back(id);
            descs.push_back(std::move(desc));
        }
        m_toMake.clear();

        std::vector<Backend::BodyId> ids(descs.size());
        m_backend->CreateBodies(descs, ids);
        for (std::size_t i = 0; i < descs.size(); ++i)
        {
            m_bodies.at(entities[i]).id = ids[i];
        }
    }

    void Bodies::PushEdits(float dt)
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
                m_backend->SetMotion(record.id, motion);
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
                m_backend->SetShape(record.id, UsableParts(parts), id, motion.mass);
            }
            record.parts = std::move(parts);

            const PhysicsMaterial material = MaterialOf(*m_entities, entity, record.parts);
            if (!Same(material, record.material))
            {
                m_backend->SetMaterial(record.id, material);
                record.material = material;
            }

            const CollisionFilter filter = EffectiveFilterOf(entity);
            if (!Same(filter, record.filter))
            {
                m_backend->SetFilter(record.id, filter);
                record.filter = filter;
            }

            const Poses::Edit edit = m_poses->Push(id, PoseKindOf(motion.motionType), world);
            if (motion.motionType == MotionType::Kinematic)
            {
                // Every step, even unmoved: otherwise the body keeps its last velocity and drifts.
                m_backend->MoveKinematic(record.id, world.pos, world.rot, dt);
            }
            else if (edit != Poses::Edit::None)
            {
                // Its joints keep the impulses they carried last step: the backend pulls them back together by
                // position, so they still hold the same load, and starting them from nothing would overshoot it and
                // could break them.
                m_backend->Teleport(record.id, world.pos, world.rot);
            }

            if (rigidBody != nullptr && motion.motionType == MotionType::Dynamic &&
                (pushVelocity || !Same(rigidBody->linearVelocity, record.linearVelocity) ||
                 !Same(rigidBody->angularVelocity, record.angularVelocity)))
            {
                m_backend->SetVelocity(record.id, rigidBody->linearVelocity, rigidBody->angularVelocity);
                record.linearVelocity = rigidBody->linearVelocity;
                record.angularVelocity = rigidBody->angularVelocity;
            }
        }
    }

    void Bodies::Queue(const Entity& entity, Pusher pusher, const Math::Vec3& amount)
    {
        if (entity.IsValid())
        {
            m_pushes.push_back(Push{entity.GetId(), pusher, amount});
        }
    }

    void Bodies::ApplyPushes()
    {
        for (const Push& push : m_pushes)
        {
            const Body* body = FindBody(push.entity);
            if (body == nullptr || !body->Active())
            {
                continue; // no body: nothing to push
            }
            (m_backend->*push.pusher)(body->id, push.amount);
        }
        m_pushes.clear();
    }

    void Bodies::HandPoses()
    {
        for (auto& [id, record] : m_bodies)
        {
            Transform world = m_poses->Baseline(id);
            if (record.motion.motionType != MotionType::Dynamic || !record.Active())
            {
                // Not written back: shown between ticks where game code put it, unless it cannot move or is inactive.
                m_poses->Hand(id, record.Active() ? PoseKindOf(record.motion.motionType) : Poses::Kind::Still, world);
                continue;
            }
            const Backend::BodyState state = m_backend->GetState(record.id);
            world.pos = state.position;
            world.rot = state.rotation;
            m_poses->Hand(id, Poses::Kind::Written, world);

            RigidBody& rigidBody = m_entities->GetEntity(id).GetComponent<RigidBody>();
            rigidBody.linearVelocity = state.linearVelocity;
            rigidBody.angularVelocity = state.angularVelocity;
            record.linearVelocity = state.linearVelocity;
            record.angularVelocity = state.angularVelocity;
        }
    }

    void Bodies::DrawColliders(DebugCanvas& canvas) const
    {
        for (const auto& [id, record] : m_bodies)
        {
            if (!record.Active())
            {
                continue;
            }
            const bool sleeping = record.motion.motionType != MotionType::Static && m_backend->IsSleeping(record.id);
            const std::uint32_t colour =
                DebugCanvas::ColliderColour(record.isTrigger, record.motion.motionType, sleeping);
            for (const Part& part : record.parts)
            {
                if (part.usable)
                {
                    canvas.Collider(part.shape, part.pose, m_poses->Baseline(id), colour);
                }
            }
        }
    }

    void Bodies::DrawMotion(DebugCanvas& canvas, bool velocities, bool bounds) const
    {
        for (const auto& [id, record] : m_bodies)
        {
            if (!record.Active())
            {
                continue; // no body to draw
            }
            if (velocities && record.motion.motionType != MotionType::Static)
            {
                const Math::Vec3 velocity = m_backend->GetState(record.id).linearVelocity;
                if (velocity != Math::Vec3(0.0f))
                {
                    const Math::Vec3& at = m_poses->Baseline(id).pos;
                    canvas.Line(at, at + velocitySeconds * velocity, DebugColour::Velocity);
                }
            }
            if (bounds)
            {
                const Math::AABB box = m_backend->Bounds(record.id);
                canvas.Writer(Math::Vec3(0.0f), Math::Quat(1.0f, 0.0f, 0.0f, 0.0f), DebugColour::Bounds)
                    .Box(box.min, box.max);
            }
        }
    }
} // namespace Fenrir::Detail
