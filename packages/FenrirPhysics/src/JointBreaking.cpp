// Breaking joints: after each step, a joint whose load went over its break force or torque is disabled, as if game
// code had disabled it, and a JointBroken tells game code why. Knows the ECS, not Jolt: it talks to Backend::World.

#include <algorithm>
#include <limits>

#include "FenrirECS/EntityList.hpp"
#include "FenrirPhysics/Events.hpp"
#include "Joints.hpp"
#include "SortAndSend.hpp"

namespace Fenrir::Detail
{
    void Joints::Break()
    {
        for (auto& [entity, record] : m_joints)
        {
            Joint& applied = record.applied;
            if (!record.HasConstraint() || !applied.enabled)
            {
                continue;
            }
            // Negative thresholds are treated as 0, as Sync warns; infinity never breaks.
            const float breakForce = std::max(applied.breakForce, 0.0f);
            const float breakTorque = std::max(applied.breakTorque, 0.0f);
            const bool finite = breakForce < std::numeric_limits<float>::infinity() ||
                                breakTorque < std::numeric_limits<float>::infinity();
            // A sleeping pair took no new load: its kept load is not checked again.
            if (!finite || !(IsAwake(applied.bodyA) || IsAwake(applied.bodyB)))
            {
                continue;
            }
            const Backend::JointLoad load = record.load;
            if (Math::Magnitude(load.force) <= breakForce && Math::Magnitude(load.torque) <= breakTorque)
            {
                continue;
            }

            m_backend->DisableJoint(record.id);
            // Into the record as well as the component, so the next diff does not take it for a game edit.
            applied.enabled = false;
            m_entities->GetEntity(entity).GetComponent<Joint>().enabled = false;
            // A disabled joint carries nothing.
            record.load = Backend::JointLoad{};
            m_broken.push_back(
                JointBroken{m_entities->HandleOf(entity), applied.bodyA, applied.bodyB, load.force, load.torque});
        }
    }

    void Joints::SendBroken(App& app)
    {
        SortAndSend(app, m_broken, [](const JointBroken& event) { return event.joint.GetId(); });
        m_broken.clear();
    }
} // namespace Fenrir::Detail
