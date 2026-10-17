// Joints (ADR-0013): FrameFromWorld, and a physics world's joints, kept in step with the Joint components. Knows the
// ECS, not Jolt: it talks to Backend::World.

#include "FenrirPhysics/Joints.hpp"

#include <set>
#include <span>
#include <string>

#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "PhysicsState.hpp"
#include "Same.hpp"
#include "WorldImpl.hpp"

namespace Fenrir
{
    namespace
    {
        bool Same(const JointFrame& a, const JointFrame& b)
        {
            return Fenrir::Same(a.position, b.position) && Fenrir::Same(a.rotation, b.rotation);
        }

        // The fields applied to an existing constraint (the fixed, ball and hinge kinds have no settings of their own
        // yet). The kind, the bodies and the frames are not: changing them recreates it.
        bool SameLive(const Joint& a, const Joint& b)
        {
            return a.enabled == b.enabled && a.collideConnected == b.collideConnected &&
                   Fenrir::Same(a.breakForce, b.breakForce) && Fenrir::Same(a.breakTorque, b.breakTorque);
        }

        // Only an empty entity is the world: a destroyed one is not.
        bool IsWorld(const Entity& body)
        {
            return body == Entity();
        }

        // A frame local to a body, in world units: its position follows the body's scale, its rotation does not.
        JointFrame Scaled(const JointFrame& frame, const Math::Vec3& scale)
        {
            return JointFrame{frame.position * scale, frame.rotation};
        }

        // `anchor` in `body`'s local space: the inverse of how the body places its frames.
        JointFrame LocalTo(const Entity& body, const JointFrame& anchor)
        {
            const Math::Vec3 position = Math::MultiplyPoint(anchor.position, Math::Inverse(GetWorldMatrix(body)));
            return JointFrame{position, Math::Conjugate(GetWorldTransform(body).rot) * anchor.rotation};
        }
    } // namespace

    JointFrames FrameFromWorld(const Entity& bodyA, const Entity& bodyB, const JointFrame& anchor)
    {
        const bool toWorld = IsWorld(bodyB);
        if (!bodyA.IsValid() || (!toWorld && !bodyB.IsValid()))
        {
#ifndef NDEBUG
            WarnThroughInstalledApp(std::string("FrameFromWorld: ") + (bodyA.IsValid() ? "bodyB" : "bodyA") +
                                    " is not alive, so it gives identity frames");
#endif
            return JointFrames{};
        }
        return JointFrames{LocalTo(bodyA, anchor), toWorld ? anchor : LocalTo(bodyB, anchor)};
    }

    const PhysicsWorld::Impl::Record* PhysicsWorld::Impl::JointBodyOf(const Entity& body)
    {
        // A handle from another scene's list names a different entity with the same id.
        if (!body.IsValid() || body != m_entities->HandleOf(body.GetId()))
        {
            return nullptr;
        }
        // Keyed by the body's own entity, so a compound body's child collider names no body.
        const auto it = m_bodies.find(body.GetId());
        if (it == m_bodies.end() || !it->second.Active() || it->second.isTrigger)
        {
            return nullptr;
        }
        return &it->second;
    }

    void PhysicsWorld::Impl::DrainJoints()
    {
        for (const ComponentChange& change : m_jointWatch.Drain())
        {
            // Removed, or removed and added again: rebuilt from what the entity has now, as a body is.
            if (const auto it = m_joints.find(change.entity); it != m_joints.end())
            {
                DropConstraint(change.entity, it->second);
                m_joints.erase(it);
            }
            if (m_entities->HasEntity(change.entity) && m_entities->GetEntity(change.entity).HasComponent<Joint>())
            {
                m_jointEntities.insert(change.entity);
            }
            else
            {
                m_jointEntities.erase(change.entity);
            }
        }
    }

    void PhysicsWorld::Impl::RemoveJointsOf(std::span<const Backend::BodyId> bodies)
    {
        for (const Backend::BodyId body : bodies)
        {
            const auto it = m_jointsOfBody.find(body.value);
            if (it == m_jointsOfBody.end())
            {
                continue;
            }
            // Copied: dropping a constraint takes it out of the index.
            const std::set<std::uint32_t> joints = it->second;
            for (const std::uint32_t joint : joints)
            {
                DropConstraint(joint, m_joints.at(joint));
            }
        }
    }

    void PhysicsWorld::Impl::MakeConstraint(std::uint32_t entity, JointRecord& record, const Backend::JointDesc& desc)
    {
        record.id = m_backend.CreateJoint(desc);
        record.bodyA = desc.bodyA;
        record.bodyB = desc.bodyB;
        m_jointsOfBody[desc.bodyA.value].insert(entity);
        if (desc.bodyB.value != Backend::BodyId{}.value)
        {
            m_jointsOfBody[desc.bodyB.value].insert(entity);
        }
    }

    void PhysicsWorld::Impl::DropConstraint(std::uint32_t entity, JointRecord& record)
    {
        if (!record.HasConstraint())
        {
            return;
        }
        m_backend.DestroyJoint(record.id);
        record.id = Backend::JointId{};
        for (const Backend::BodyId body : {record.bodyA, record.bodyB})
        {
            const auto it = m_jointsOfBody.find(body.value);
            if (it != m_jointsOfBody.end() && it->second.erase(entity) > 0 && it->second.empty())
            {
                m_jointsOfBody.erase(it);
            }
        }
    }

    void PhysicsWorld::Impl::SyncJoints()
    {
        for (const std::uint32_t entity : m_jointEntities)
        {
            const Joint& joint = m_entities->GetEntity(entity).GetComponent<Joint>();
            JointRecord& record = m_joints[entity];

            const Record* a = JointBodyOf(joint.bodyA);
            const bool toWorld = IsWorld(joint.bodyB);
            const Record* b = toWorld ? nullptr : JointBodyOf(joint.bodyB);
            if (a == nullptr || (!toWorld && (b == nullptr || b == a)))
            {
                // Inactive: it names no pair of made body entities, and has no constraint until it does.
                DropConstraint(entity, record);
                record.applied = joint;
                continue;
            }

            const Math::Vec3 scaleA = a->world.scale;
            const Math::Vec3 scaleB = toWorld ? Math::Vec3(1.0f) : b->world.scale;
            // A frame's place on its body follows the body's scale, so a scale change moves it, as a frame edit does.
            const bool recreate = !record.HasConstraint() || joint.kind.Type() != record.applied.kind.Type() ||
                                  joint.bodyA != record.applied.bodyA || joint.bodyB != record.applied.bodyB ||
                                  !Same(joint.frameA, record.applied.frameA) ||
                                  !Same(joint.frameB, record.applied.frameB) || !Fenrir::Same(scaleA, record.scaleA) ||
                                  !Fenrir::Same(scaleB, record.scaleB);
            if (recreate)
            {
                DropConstraint(entity, record);
                Backend::JointDesc desc;
                desc.bodyA = a->id;
                desc.bodyB = toWorld ? Backend::BodyId{} : b->id;
                desc.frameA = Scaled(joint.frameA, scaleA);
                desc.frameB = toWorld ? joint.frameB : Scaled(joint.frameB, scaleB);
                desc.kind = joint.kind;
                desc.enabled = joint.enabled;
                desc.priority = entity;
                MakeConstraint(entity, record, desc);
                record.scaleA = scaleA;
                record.scaleB = scaleB;
                record.applied = joint;
            }
            else if (!SameLive(joint, record.applied))
            {
                m_backend.UpdateJoint(record.id, joint.kind, joint.enabled);
                record.applied = joint;
            }
        }
    }
} // namespace Fenrir
