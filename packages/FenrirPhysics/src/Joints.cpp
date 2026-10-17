// PROTOTYPE (joints spike, ticket 27): joints through the model of ADR-0013 and tickets 23 to 26, only as far as a
// hinged door and a hanging chain need. Knows the ECS, not Jolt: it talks to Backend::World. Never merged.

#include "FenrirPhysics/Joints.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>

#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "WorldImpl.hpp"

namespace Fenrir
{
    namespace
    {
        // Bit for bit, as the body diff compares: any value game code writes is an edit.
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

        bool Same(const JointFrame& a, const JointFrame& b)
        {
            return Same(a.position, b.position) && Same(a.rotation, b.rotation);
        }

        bool Same(const JointSpring& a, const JointSpring& b)
        {
            return Same(a.frequency, b.frequency) && Same(a.damping, b.damping);
        }

        bool Same(const HingeJoint& a, const HingeJoint& b)
        {
            return a.limits.enabled == b.limits.enabled && Same(a.limits.min, b.limits.min) &&
                   Same(a.limits.max, b.limits.max) && Same(a.limits.spring, b.limits.spring) &&
                   a.motor.mode == b.motor.mode && Same(a.motor.targetVelocity, b.motor.targetVelocity) &&
                   Same(a.motor.targetPosition, b.motor.targetPosition) && Same(a.motor.maxForce, b.motor.maxForce) &&
                   Same(a.motor.spring, b.motor.spring) && Same(a.friction, b.friction);
        }

        // The per-kind settings and shared fields that are live edits (ticket 23); the kind, bodies and frames are not.
        bool SameLive(const Joint& a, const Joint& b)
        {
            const bool sameKind = !a.kind.Is<HingeJoint>() || Same(a.kind.As<HingeJoint>(), b.kind.As<HingeJoint>());
            return sameKind && a.enabled == b.enabled && a.collideConnected == b.collideConnected &&
                   Same(a.breakForce, b.breakForce) && Same(a.breakTorque, b.breakTorque);
        }

        bool IsWorld(const Entity& body)
        {
            return body == Entity();
        }

        // A frame local to a body, in world units: frames scale with their body (ADR-0013).
        JointFrame Scaled(const JointFrame& frame, const Math::Vec3& scale)
        {
            return JointFrame{frame.position * scale, frame.rotation};
        }

        JointFrame InWorld(const JointFrame& frame, const Math::Vec3& position, const Math::Quat& rotation)
        {
            return JointFrame{position + rotation * frame.position, rotation * frame.rotation};
        }
    } // namespace

    JointFrames FrameFromWorld(const Entity& bodyA, const Entity& bodyB, const JointFrame& anchor)
    {
        const auto local = [&anchor](const Entity& body) {
            if (!body.IsValid())
            {
                return anchor; // the world
            }
            const Transform world = GetWorldTransform(body);
            const Math::Quat inverse = Math::Conjugate(world.rot);
            return JointFrame{inverse * (anchor.position - world.pos) / world.scale, inverse * anchor.rotation};
        };
        return JointFrames{local(bodyA), local(bodyB)};
    }

    Fenrir::JointState PhysicsWorld::JointState(const Entity& joint) const
    {
        return joint.IsValid() ? m_impl->StateOf(joint.GetId()) : Fenrir::JointState{};
    }

    std::string PhysicsWorld::Impl::InvalidReason(const Joint& joint)
    {
        const auto bodyProblem = [this](const Entity& body, const char* name) -> std::string {
            if (!body.IsValid())
            {
                return std::string(name) + " is empty or destroyed";
            }
            if (body != m_entities->HandleOf(body.GetId()))
            {
                return std::string(name) + " is in another scene";
            }
            const auto it = m_bodies.find(body.GetId());
            if (it == m_bodies.end() || !it->second.Active() || it->second.isTrigger)
            {
                return std::string(name) + " (entity " + std::to_string(body.GetId()) + ") is not a body entity";
            }
            return {};
        };
        if (std::string problem = bodyProblem(joint.bodyA, "bodyA"); !problem.empty())
        {
            return problem;
        }
        if (IsWorld(joint.bodyB))
        {
            return {};
        }
        if (joint.bodyA == joint.bodyB)
        {
            return "bodyA and bodyB are the same body";
        }
        return bodyProblem(joint.bodyB, "bodyB");
    }

    void PhysicsWorld::Impl::RemoveJointsOf(std::span<const Backend::BodyId> bodies)
    {
        for (auto& [entity, record] : m_joints)
        {
            if (!record.HasConstraint())
            {
                continue;
            }
            for (const Backend::BodyId body : bodies)
            {
                if (body.value == record.bodyIdA.value || body.value == record.bodyIdB.value)
                {
                    m_backend.DestroyJoint(record.id);
                    record.id = Backend::JointId{};
                    break;
                }
            }
        }
    }

    void PhysicsWorld::Impl::SyncJoints()
    {
        for (const ComponentChange& change : m_jointWatch.Drain())
        {
            if (m_entities->HasEntity(change.entity) && m_entities->GetEntity(change.entity).HasComponent<Joint>())
            {
                m_jointEntities.insert(change.entity);
            }
            else
            {
                m_jointEntities.erase(change.entity);
            }
        }

        // Removed joints, constraint and record.
        for (auto it = m_joints.begin(); it != m_joints.end();)
        {
            if (m_jointEntities.contains(it->first))
            {
                ++it;
                continue;
            }
            if (it->second.HasConstraint())
            {
                m_backend.DestroyJoint(it->second.id);
            }
            it = m_joints.erase(it);
        }

        // In joint entity order: created in that order, and each given that order as its solve priority.
        for (const std::uint32_t entity : m_jointEntities)
        {
            const Joint& joint = m_entities->GetEntity(entity).GetComponent<Joint>();
            JointRecord& record = m_joints[entity];

            const std::string invalid = InvalidReason(joint);
            if (!invalid.empty())
            {
                if (record.HasConstraint())
                {
                    m_backend.DestroyJoint(record.id); // wakes the body left
                    record.id = Backend::JointId{};
                }
                if (!record.inactive)
                {
                    m_logger->Warn("The Joint of entity " + std::to_string(entity) + " is inactive: " + invalid);
                    record.inactive = true;
                }
                record.load = Backend::JointLoad{};
                record.applied = joint;
                continue;
            }
            record.inactive = false;

            const Record& a = m_bodies.at(joint.bodyA.GetId());
            const Record* b = IsWorld(joint.bodyB) ? nullptr : &m_bodies.at(joint.bodyB.GetId());
            const Math::Vec3 scaleA = a.world.scale;
            const Math::Vec3 scaleB = b != nullptr ? b->world.scale : Math::Vec3(1.0f);
            const Backend::BodyId idB = b != nullptr ? b->id : Backend::BodyId{};

            // The kind, a body or a frame (scale moves a frame) changing recreates the constraint (ADR-0013).
            const bool recreate = !record.HasConstraint() || joint.kind.Type() != record.applied.kind.Type() ||
                                  joint.bodyA != record.applied.bodyA || joint.bodyB != record.applied.bodyB ||
                                  !Same(joint.frameA, record.applied.frameA) ||
                                  !Same(joint.frameB, record.applied.frameB) || !Same(scaleA, record.scaleA) ||
                                  !Same(scaleB, record.scaleB) || a.id.value != record.bodyIdA.value ||
                                  idB.value != record.bodyIdB.value;
            if (recreate)
            {
                if (record.HasConstraint())
                {
                    m_backend.DestroyJoint(record.id);
                }
                Backend::JointDesc desc;
                desc.bodyA = a.id;
                desc.bodyB = idB;
                desc.frameA = Scaled(joint.frameA, scaleA);
                desc.frameB = b != nullptr ? Scaled(joint.frameB, scaleB) : joint.frameB;
                desc.kind = joint.kind;
                desc.enabled = joint.enabled;
                desc.collideConnected = joint.collideConnected;
                desc.priority = entity;
                record.id = m_backend.CreateJoint(desc);
                record.applied = joint;
                record.scaleA = scaleA;
                record.scaleB = scaleB;
                record.bodyIdA = a.id;
                record.bodyIdB = idB;
                continue;
            }
            if (!SameLive(joint, record.applied))
            {
                m_backend.UpdateJoint(record.id, joint.kind, joint.enabled, joint.collideConnected);
                record.applied = joint;
            }
            if (m_teleported.contains(joint.bodyA.GetId()) ||
                (!IsWorld(joint.bodyB) && m_teleported.contains(joint.bodyB.GetId())))
            {
                m_backend.ResetWarmStart(record.id);
            }
        }
        m_teleported.clear();
    }

    void PhysicsWorld::Impl::BreakJoints()
    {
        for (auto& [entity, record] : m_joints)
        {
            if (!record.HasConstraint())
            {
                continue;
            }
            // A sleeping joint keeps the load it last had awake.
            const bool awake =
                !m_backend.IsSleeping(record.bodyIdA) ||
                (record.bodyIdB.value != Backend::BodyId{}.value && !m_backend.IsSleeping(record.bodyIdB));
            if (!awake)
            {
                continue;
            }
            record.load = m_backend.Load(record.id);
            const Joint& applied = record.applied;
            if (!applied.enabled || (Math::Magnitude(record.load.force) <= applied.breakForce &&
                                     Math::Magnitude(record.load.torque) <= applied.breakTorque))
            {
                continue;
            }
            Joint& joint = m_entities->GetEntity(entity).GetComponent<Joint>();
            joint.enabled = false;
            record.applied.enabled = false;
            m_backend.UpdateJoint(record.id, joint.kind, false, joint.collideConnected);
            m_broken.push_back(JointBroken{m_entities->HandleOf(entity), joint.bodyA, joint.bodyB, record.load.force,
                                           record.load.torque});
        }
    }

    Fenrir::JointState PhysicsWorld::Impl::StateOf(std::uint32_t joint) const
    {
        const auto it = m_joints.find(joint);
        if (it == m_joints.end() || !it->second.HasConstraint())
        {
            return Fenrir::JointState{};
        }
        const JointRecord& record = it->second;
        Fenrir::JointState state;
        if (record.applied.enabled)
        {
            state.force = record.load.force;
            state.torque = record.load.torque;
        }
        // From the last step's simulated poses, never the interpolated Transforms (ticket 24).
        const Backend::BodyState a = m_backend.GetState(record.bodyIdA);
        const JointFrame frameA = InWorld(Scaled(record.applied.frameA, record.scaleA), a.position, a.rotation);
        JointFrame frameB = record.applied.frameB;
        if (record.bodyIdB.value != Backend::BodyId{}.value)
        {
            const Backend::BodyState b = m_backend.GetState(record.bodyIdB);
            frameB = InWorld(Scaled(record.applied.frameB, record.scaleB), b.position, b.rotation);
        }
        const Math::Quat inverse = Math::Conjugate(frameA.rotation);
        state.relative = JointFrame{inverse * (frameB.position - frameA.position), inverse * frameB.rotation};
        if (record.applied.kind.Is<HingeJoint>())
        {
            const Math::Quat& q = state.relative.rotation;
            float angle = 2.0f * std::atan2(q.x, q.w);
            if (angle > std::numbers::pi_v<float>)
            {
                angle -= 2.0f * std::numbers::pi_v<float>;
            }
            else if (angle <= -std::numbers::pi_v<float>)
            {
                angle += 2.0f * std::numbers::pi_v<float>;
            }
            state.position = angle;
        }
        return state;
    }
} // namespace Fenrir
