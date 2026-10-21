// Joints (ADR-0013): FrameFromWorld, and a physics world's joints, kept in step with the Joint components. Knows the
// ECS, not Jolt: it talks to Backend::World.

#include "Joints.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <numbers>
#include <set>
#include <span>
#include <string>

#include "DebugCanvas.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "PhysicsState.hpp"
#include "Same.hpp"
#include "WarnOnce.hpp"

namespace Fenrir
{
    namespace
    {
        bool Same(const JointFrame& a, const JointFrame& b)
        {
            return Fenrir::Same(a.position, b.position) && Fenrir::Same(a.rotation, b.rotation);
        }

        bool Same(const JointSpring& a, const JointSpring& b)
        {
            return Fenrir::Same(a.frequency, b.frequency) && Fenrir::Same(a.damping, b.damping);
        }

        bool Same(const JointLimits& a, const JointLimits& b)
        {
            return a.enabled == b.enabled && Fenrir::Same(a.min, b.min) && Fenrir::Same(a.max, b.max) &&
                   Same(a.spring, b.spring);
        }

        bool Same(const JointMotor& a, const JointMotor& b)
        {
            return a.mode == b.mode && Fenrir::Same(a.targetVelocity, b.targetVelocity) &&
                   Fenrir::Same(a.targetPosition, b.targetPosition) && Fenrir::Same(a.maxForce, b.maxForce) &&
                   Same(a.spring, b.spring);
        }

        bool Same(const SwingTwistMotor& a, const SwingTwistMotor& b)
        {
            return a.mode == b.mode && Fenrir::Same(a.targetRotation, b.targetRotation) &&
                   Fenrir::Same(a.targetAngularVelocity, b.targetAngularVelocity) &&
                   Fenrir::Same(a.maxTorque, b.maxTorque) && Same(a.spring, b.spring);
        }

        // The settings of a kind applied to an existing constraint: all of them. Both kinds are the same.
        bool SameLiveSettings(const JointKind& a, const JointKind& b)
        {
            switch (a.Type())
            {
            case JointType::Distance: {
                const DistanceJoint& distanceA = a.As<DistanceJoint>();
                const DistanceJoint& distanceB = b.As<DistanceJoint>();
                return Fenrir::Same(distanceA.minDistance, distanceB.minDistance) &&
                       Fenrir::Same(distanceA.maxDistance, distanceB.maxDistance) &&
                       Same(distanceA.spring, distanceB.spring);
            }
            case JointType::Hinge: {
                const HingeJoint& hingeA = a.As<HingeJoint>();
                const HingeJoint& hingeB = b.As<HingeJoint>();
                return Same(hingeA.limits, hingeB.limits) && Same(hingeA.motor, hingeB.motor) &&
                       Fenrir::Same(hingeA.friction, hingeB.friction);
            }
            case JointType::Slider: {
                const SliderJoint& sliderA = a.As<SliderJoint>();
                const SliderJoint& sliderB = b.As<SliderJoint>();
                return Same(sliderA.limits, sliderB.limits) && Same(sliderA.motor, sliderB.motor) &&
                       Fenrir::Same(sliderA.friction, sliderB.friction);
            }
            case JointType::SwingTwist: {
                const SwingTwistJoint& swingTwistA = a.As<SwingTwistJoint>();
                const SwingTwistJoint& swingTwistB = b.As<SwingTwistJoint>();
                return Fenrir::Same(swingTwistA.swingYLimit, swingTwistB.swingYLimit) &&
                       Fenrir::Same(swingTwistA.swingZLimit, swingTwistB.swingZLimit) &&
                       Fenrir::Same(swingTwistA.twistMin, swingTwistB.twistMin) &&
                       Fenrir::Same(swingTwistA.twistMax, swingTwistB.twistMax) &&
                       Fenrir::Same(swingTwistA.friction, swingTwistB.friction) &&
                       Same(swingTwistA.motor, swingTwistB.motor);
            }
            default:
                return true;
            }
        }

        // The fields applied to an existing constraint. The kind, the bodies and the frames are not: changing them
        // recreates it.
        bool SameLive(const Joint& a, const Joint& b)
        {
            return a.enabled == b.enabled && a.collideConnected == b.collideConnected &&
                   Fenrir::Same(a.breakForce, b.breakForce) && Fenrir::Same(a.breakTorque, b.breakTorque) &&
                   SameLiveSettings(a.kind, b.kind);
        }

        // The length of a joint frame's axes: frameA's long, frameB's short, so both show where they coincide.
        constexpr float jointAxisA = 0.5f;
        constexpr float jointAxisB = 0.25f;

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

        // The kinds of out-of-range value a joint can hold, each warned about once per joint.
        enum Problem : std::uint32_t
        {
            NegativeFrequency = 1u << 0,
            FrequencyTooHigh = 1u << 1,
            NegativeDamping = 1u << 2,
            NegativeForce = 1u << 3,
            NegativeFriction = 1u << 4,
            NegativeBreakThreshold = 1u << 5,
            LimitExcludesRestPose = 1u << 6,
            HingeLimitBeyondHalfTurn = 1u << 7,
            ConeOutOfRange = 1u << 8,
            TwistExcludesRestPose = 1u << 9,
            TwistBeyondHalfTurn = 1u << 10,
            NegativeMinDistance = 1u << 11,
            MaxBelowMinDistance = 1u << 12,
        };

        // Corrects a joint's out-of-range values as physics applies it, warning about each kind of value the first
        // time the joint holds one.
        class Corrector
        {
          public:
            Corrector(std::uint32_t entity, std::uint32_t& warned, ILogger& logger, float maxFrequency)
                : m_entity(entity), m_warned(&warned), m_logger(&logger), m_maxFrequency(maxFrequency)
            {
            }

            [[nodiscard]] Joint Corrected(const Joint& joint)
            {
                Joint corrected = joint;
                NotNegative(corrected.breakForce, NegativeBreakThreshold, "a negative break threshold is treated as 0");
                NotNegative(corrected.breakTorque, NegativeBreakThreshold,
                            "a negative break threshold is treated as 0");
                switch (corrected.kind.Type())
                {
                case JointType::Distance:
                    Correct(corrected.kind.As<DistanceJoint>());
                    break;
                case JointType::Hinge:
                    Correct(corrected.kind.As<HingeJoint>());
                    break;
                case JointType::Slider:
                    Correct(corrected.kind.As<SliderJoint>());
                    break;
                case JointType::SwingTwist:
                    Correct(corrected.kind.As<SwingTwistJoint>());
                    break;
                default:
                    break;
                }
                return corrected;
            }

          private:
            void Warn(Problem problem, const char* message)
            {
                Detail::WarnOnce(*m_warned, problem, *m_logger,
                                 "The Joint on entity " + std::to_string(m_entity) + ": " + message);
            }

            void NotNegative(float& value, Problem problem, const char* message)
            {
                if (value < 0.0f)
                {
                    value = 0.0f;
                    Warn(problem, message);
                }
            }

            void Correct(JointSpring& spring)
            {
                NotNegative(spring.frequency, NegativeFrequency, "a negative spring frequency is treated as 0");
                NotNegative(spring.damping, NegativeDamping, "a negative damping ratio is treated as 0");
                if (spring.frequency > m_maxFrequency)
                {
                    spring.frequency = m_maxFrequency;
                    Warn(FrequencyTooHigh, "a spring frequency above half the tick rate is lowered to it");
                }
            }

            // The rest pose lies inside a range: one excluding it, or reversed, is widened to take in both its ends and
            // the rest pose.
            void HoldRestPose(float& min, float& max, Problem problem, const char* message)
            {
                if (min <= 0.0f && 0.0f <= max)
                {
                    return;
                }
                const float low = std::min({min, max, 0.0f});
                max = std::max({min, max, 0.0f});
                min = low;
                Warn(problem, message);
            }

            // Moves `value` into [low, high], warning if it was outside.
            void Within(float& value, float low, float high, Problem problem, const char* message)
            {
                if (value < low || value > high)
                {
                    value = std::clamp(value, low, high);
                    Warn(problem, message);
                }
            }

            void WithinHalfTurn(float& angle, Problem problem, const char* message)
            {
                Within(angle, -std::numbers::pi_v<float>, std::numbers::pi_v<float>, problem, message);
            }

            // A hinge's or a slider's limits, and the motor they hold. A hinge's limits lie within a half turn.
            void Correct(JointLimits& limits, JointMotor& motor, bool hinge)
            {
                Correct(limits.spring);
                HoldRestPose(limits.min, limits.max, LimitExcludesRestPose,
                             "limits excluding the rest pose are widened to take it in");
                if (hinge)
                {
                    WithinHalfTurn(limits.min, HingeLimitBeyondHalfTurn,
                                   "hinge limits beyond a half turn are "
                                   "lowered to it");
                    WithinHalfTurn(limits.max, HingeLimitBeyondHalfTurn,
                                   "hinge limits beyond a half turn are "
                                   "lowered to it");
                }
                Correct(motor.spring);
                NotNegative(motor.maxForce, NegativeForce, "a negative motor force is treated as 0");
                // Silently: a target beyond a limit is where a motor drives a door shut against its frame.
                if (limits.enabled)
                {
                    motor.targetPosition = std::clamp(motor.targetPosition, limits.min, limits.max);
                }
            }

            void Correct(DistanceJoint& distance)
            {
                Correct(distance.spring);
                NotNegative(distance.minDistance, NegativeMinDistance, "a negative minimum distance is raised to 0");
                if (distance.maxDistance < distance.minDistance)
                {
                    distance.maxDistance = distance.minDistance;
                    Warn(MaxBelowMinDistance, "a maximum distance below the minimum is raised to it");
                }
            }

            void Correct(HingeJoint& hinge)
            {
                Correct(hinge.limits, hinge.motor, true);
                NotNegative(hinge.friction, NegativeFriction, "a negative friction is treated as 0");
            }

            void Correct(SliderJoint& slider)
            {
                Correct(slider.limits, slider.motor, false);
                NotNegative(slider.friction, NegativeFriction, "a negative friction is treated as 0");
            }

            void Correct(SwingTwistJoint& swingTwist)
            {
                for (float* cone : {&swingTwist.swingYLimit, &swingTwist.swingZLimit})
                {
                    Within(*cone, 0.0f, std::numbers::pi_v<float>, ConeOutOfRange,
                           "a swing limit outside 0 to a half turn is moved into it");
                }
                WithinHalfTurn(swingTwist.twistMin, TwistBeyondHalfTurn,
                               "twist limits beyond a half turn are "
                               "lowered to it");
                WithinHalfTurn(swingTwist.twistMax, TwistBeyondHalfTurn,
                               "twist limits beyond a half turn are "
                               "lowered to it");
                HoldRestPose(swingTwist.twistMin, swingTwist.twistMax, TwistExcludesRestPose,
                             "a twist range excluding the rest pose is widened to take it in");
                NotNegative(swingTwist.friction, NegativeFriction, "a negative friction is treated as 0");
                Correct(swingTwist.motor.spring);
                NotNegative(swingTwist.motor.maxTorque, NegativeForce, "a negative motor torque is treated as 0");
            }

            std::uint32_t m_entity;
            std::uint32_t* m_warned;
            ILogger* m_logger;
            float m_maxFrequency;
        };
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
} // namespace Fenrir

namespace Fenrir::Detail
{
    Joints::Joints(EntityList& entities, ILogger& logger, Backend::World& backend, const Bodies& bodies,
                   const Poses& poses)
        : m_entities(&entities), m_logger(&logger), m_backend(&backend), m_bodies(&bodies), m_poses(&poses),
          m_jointEntities(entities, ComponentType::Native<Joint>())
    {
    }

    const Bodies::Record* Joints::BodyOf(const Entity& body) const
    {
        // A handle from another scene's list names a different entity with the same id.
        if (!body.IsValid() || body != m_entities->HandleOf(body.GetId()))
        {
            return nullptr;
        }
        // Keyed by the body's own entity, so a compound body's child collider names no body.
        const Bodies::Record* record = m_bodies->RecordOf(body.GetId());
        if (record == nullptr || !record->Active() || record->isTrigger)
        {
            return nullptr;
        }
        return record;
    }

    std::string Joints::WhyInactive(const Joint& joint) const
    {
        if (IsWorld(joint.bodyA))
        {
            return "bodyA is empty (only bodyB can be the world)";
        }
        if (joint.bodyA == joint.bodyB)
        {
            return "bodyA and bodyB are the same entity";
        }
        const auto why = [this](const Entity& body, const char* name) -> std::string {
            if (!body.IsValid())
            {
                return std::string(name) + " is not alive";
            }
            if (body != m_entities->HandleOf(body.GetId()))
            {
                return std::string(name) + " is in another scene";
            }
            if (IsCharacterCollider(body))
            {
                return std::string(name) + " (entity " + std::to_string(body.GetId()) +
                       ") is a character body, which a joint cannot join";
            }
            return std::string(name) + " (entity " + std::to_string(body.GetId()) +
                   ") is not a body entity: a RigidBody's, or a standalone static collider's, with a collider physics "
                   "could make";
        };
        // The side whose body is missing: bodyA's if both are.
        return BodyOf(joint.bodyA) != nullptr ? why(joint.bodyB, "bodyB") : why(joint.bodyA, "bodyA");
    }

    void Joints::Drain()
    {
        for (const std::uint32_t entity : m_jointEntities.Drain())
        {
            // Removed, or removed and added again: rebuilt from what the entity has now, as a body is.
            if (const auto it = m_joints.find(entity); it != m_joints.end())
            {
                DropConstraint(entity, it->second);
                m_joints.erase(it);
            }
        }
    }

    void Joints::DropOn(std::span<const Backend::BodyId> bodies)
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

    void Joints::MakeConstraint(std::uint32_t entity, JointRecord& record, const Backend::JointDesc& desc)
    {
        record.id = m_backend->CreateJoint(desc);
        record.bodyA = desc.bodyA;
        record.bodyB = desc.bodyB;
        m_jointsOfBody[desc.bodyA.value].insert(entity);
        if (desc.bodyB.value != Backend::BodyId{}.value)
        {
            m_jointsOfBody[desc.bodyB.value].insert(entity);
        }
    }

    void Joints::DropConstraint(std::uint32_t entity, JointRecord& record)
    {
        if (!record.HasConstraint())
        {
            return;
        }
        m_backend->DestroyJoint(record.id);
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

    void Joints::Sync(float dt)
    {
        // A spring faster than half the tick rate cannot be stepped.
        const float maxFrequency = 0.5f / dt;
        for (const std::uint32_t entity : m_jointEntities.Entities())
        {
            const Joint& joint = m_entities->GetEntity(entity).GetComponent<Joint>();
            JointRecord& record = m_joints[entity];

            const Bodies::Record* a = BodyOf(joint.bodyA);
            const bool toWorld = IsWorld(joint.bodyB);
            const Bodies::Record* b = toWorld ? nullptr : BodyOf(joint.bodyB);
            if (a == nullptr || (!toWorld && (b == nullptr || b == a)))
            {
                // Inactive: it names no pair of made body entities, and has no constraint until it does. Dropping it
                // wakes the body left.
                if (!record.inactive)
                {
                    m_logger->Warn("The Joint on entity " + std::to_string(entity) +
                                   " is inactive: " + WhyInactive(joint) +
                                   ". It does nothing until both its bodies are body entities of its scene.");
                    record.inactive = true;
                }
                DropConstraint(entity, record);
                record.applied = joint;
                continue;
            }
            record.inactive = false;

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
                desc.kind = Corrector(entity, record.warned, *m_logger, maxFrequency).Corrected(joint).kind;
                desc.enabled = joint.enabled;
                desc.collideConnected = joint.collideConnected;
                desc.priority = entity;
                MakeConstraint(entity, record, desc);
                record.scaleA = scaleA;
                record.scaleB = scaleB;
                record.applied = joint;
            }
            else if (!SameLive(joint, record.applied))
            {
                const Joint corrected = Corrector(entity, record.warned, *m_logger, maxFrequency).Corrected(joint);
                m_backend->UpdateJoint(record.id, corrected.kind, joint.enabled, joint.collideConnected);
                record.applied = joint;
            }
        }
    }

    void Joints::Draw(DebugCanvas& canvas) const
    {
        // Every kind draws the same two frames. Shapes of a kind's own (limit arcs, cones) would go here, by kind.
        const auto frame = [&canvas](const JointFrame& local, const Transform* body, const Math::Vec3& scale,
                                     float length, std::array<std::uint32_t, 3> colours) {
            // A frame's position follows its body's scale and its rotation does not, as the constraint was made; the
            // world end's frame is already in world space.
            const Math::Vec3 position =
                body == nullptr ? local.position : body->pos + body->rot * (local.position * scale);
            const Math::Quat rotation = body == nullptr ? local.rotation : body->rot * local.rotation;
            const std::array<Math::Vec3, 3> axes = {xAxis, yAxis, zAxis};
            for (std::size_t i = 0; i < axes.size(); ++i)
            {
                canvas.Writer(position, rotation, colours[i])(Math::Vec3(0.0f), length * axes[i]);
            }
        };
        for (const auto& [entity, record] : m_joints)
        {
            const Joint& joint = record.applied;
            if (!record.HasConstraint())
            {
                // Inactive: each frame whose body is still a body, in grey. Never the world's: nothing holds it there.
                constexpr std::array<std::uint32_t, 3> grey = {DebugColour::InactiveJoint, DebugColour::InactiveJoint,
                                                               DebugColour::InactiveJoint};
                if (const Bodies::Record* a = BodyOf(joint.bodyA))
                {
                    frame(joint.frameA, &a->world, a->world.scale, jointAxisA, grey);
                }
                if (const Bodies::Record* b = BodyOf(joint.bodyB))
                {
                    frame(joint.frameB, &b->world, b->world.scale, jointAxisB, grey);
                }
                continue;
            }
            const Bodies::Record* a = BodyOf(joint.bodyA);
            const bool toWorld = joint.bodyB == Entity();
            const Bodies::Record* b = toWorld ? nullptr : BodyOf(joint.bodyB);
            if (a == nullptr || (!toWorld && b == nullptr))
            {
                continue; // not expected: Sync drops a constraint before its bodies go
            }
            // A disabled joint, broken or switched off by game code, is dimmed.
            const auto colour = [&joint](std::uint32_t full) {
                return joint.enabled ? full : DebugColour::Dimmed(full);
            };
            frame(joint.frameA, &a->world, record.scaleA, jointAxisA,
                  {colour(DebugColour::JointAX), colour(DebugColour::JointAY), colour(DebugColour::JointAZ)});
            frame(joint.frameB, toWorld ? nullptr : &b->world, record.scaleB, jointAxisB,
                  {colour(DebugColour::JointBX), colour(DebugColour::JointBY), colour(DebugColour::JointBZ)});
        }
    }
} // namespace Fenrir::Detail
