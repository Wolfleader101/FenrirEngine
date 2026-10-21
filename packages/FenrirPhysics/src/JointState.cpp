// Joint state: each joint's load, kept from the last step its bodies were awake for, and the pose readings, computed
// from the bodies' poses after the last step. Knows the ECS, not Jolt: the load comes from Backend::World.

#include <cmath>
#include <numbers>

#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "WorldImpl.hpp"

namespace Fenrir
{
    namespace
    {
        // A frame on a body whose origin is at `pose`, its position in world units (the body's scale applied), in the
        // world.
        JointFrame InWorld(const JointFrame& frame, const Transform& pose)
        {
            return JointFrame{pose.pos + Math::Rotate(pose.rot, frame.position), pose.rot * frame.rotation};
        }

        // The turn about frameA's X that `relative` makes, wrapped to (-pi, pi].
        float HingeAngle(const Math::Quat& relative)
        {
            constexpr float pi = std::numbers::pi_v<float>;
            float angle = 2.0f * std::atan2(relative.x, relative.w);
            if (angle > pi)
            {
                angle -= 2.0f * pi;
            }
            else if (angle <= -pi)
            {
                angle += 2.0f * pi;
            }
            return angle;
        }
    } // namespace

    bool PhysicsWorld::Impl::IsAwake(const Entity& body) const
    {
        const Record* record = BodyView().RecordOf(body.GetId());
        return record != nullptr && record->Active() && record->motion.motionType != MotionType::Static &&
               !m_backend.IsSleeping(record->id);
    }

    void PhysicsWorld::Impl::MeasureJoints()
    {
        for (auto& [entity, record] : m_joints)
        {
            if (!record.HasConstraint() || !record.applied.enabled)
            {
                record.load = Backend::JointLoad{};
            }
            // A sleeping pair is not solved: it keeps the load it had when it fell asleep.
            else if (IsAwake(record.applied.bodyA) || IsAwake(record.applied.bodyB))
            {
                record.load = m_backend.Load(record.id);
            }
        }
    }

    JointState PhysicsWorld::Impl::StateOf(const Entity& joint) const
    {
        // A handle from another scene's list names a different entity with the same id.
        if (!joint.IsValid() || joint != m_entities->HandleOf(joint.GetId()))
        {
            return JointState{};
        }
        const auto it = m_joints.find(joint.GetId());
        if (it == m_joints.end() || !it->second.HasConstraint())
        {
            return JointState{};
        }
        const JointRecord& record = it->second;
        const Record* a = BodyView().RecordOf(record.applied.bodyA.GetId());
        if (a == nullptr)
        {
            return JointState{};
        }

        // From the bodies' poses after the last step, never their interpolated Transforms.
        const JointFrame frameA =
            InWorld(JointFrame{record.applied.frameA.position * record.scaleA, record.applied.frameA.rotation},
                    a->shown.current);
        JointFrame frameB = record.applied.frameB;
        if (record.applied.bodyB != Entity())
        {
            const Record* b = BodyView().RecordOf(record.applied.bodyB.GetId());
            if (b == nullptr)
            {
                return JointState{};
            }
            frameB = InWorld(JointFrame{record.applied.frameB.position * record.scaleB, record.applied.frameB.rotation},
                             b->shown.current);
        }
        const Math::Quat toA = Math::Conjugate(frameA.rotation);

        JointState state;
        state.force = record.load.force;
        state.torque = record.load.torque;
        state.relative = JointFrame{Math::Rotate(toA, frameB.position - frameA.position), toA * frameB.rotation};
        switch (record.applied.kind.Type())
        {
        case JointType::Hinge:
            state.position = HingeAngle(state.relative.rotation);
            break;
        case JointType::Slider:
            state.position = state.relative.position.x;
            break;
        case JointType::Distance:
            state.position = Math::Magnitude(state.relative.position);
            break;
        case JointType::Fixed:
        case JointType::Ball:
        case JointType::SwingTwist:
            break;
        }
        return state;
    }

    JointState PhysicsWorld::GetJointState(const Entity& joint) const
    {
        return m_impl->StateOf(joint);
    }
} // namespace Fenrir
