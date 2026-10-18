#pragma once

#include <cassert>
#include <cstdint>
#include <limits>
#include <numbers>
#include <type_traits>

#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    /**
     * @brief A pose on a body: local to the body entity's origin, or in world space for the world end of a joint. Its
     * position follows the body's world scale (an anchor on a crate's corner stays on the corner); its rotation does
     * not. Its local X axis is a hinge's axis, a slider's direction and a swing-twist joint's twist axis.
     */
    struct JointFrame
    {
        Math::Vec3 position = Math::Vec3(0.0f);
        Math::Quat rotation = Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
    };

    /** @brief How hard a part of a joint pulls back. */
    struct JointSpring
    {
        /** @brief Hertz: 0 is rigid. Negative is treated as 0, and above half the tick rate is lowered to it. */
        float frequency = 0.0f;
        /** @brief The damping ratio: 1 is critical. Negative is treated as 0. */
        float damping = 1.0f;
    };

    /**
     * @brief The range a hinge or slider is kept in, measured from the rest pose. Disabled, it is free.
     *
     * The rest pose lies inside the range (min <= 0 <= max): a range excluding it, or with min above max, is widened
     * to take in both ends and the rest pose, with a warning. min == max holds the joint at the rest pose. A hinge's
     * limits lie within a half turn either way, and are lowered to it with a warning.
     *
     * A hard limit (a rigid spring) always beats the joint's motor, however strong. A soft limit is a spring, which a
     * motor or a blow may push past.
     */
    struct JointLimits
    {
        bool enabled = false;
        /** @brief Radians for a hinge, metres for a slider. */
        float min = 0.0f;
        /** @brief Radians for a hinge, metres for a slider. */
        float max = 0.0f;
        /** @brief Makes the limit soft. */
        JointSpring spring = {};
    };

    /** @brief What a motor drives: nothing, a velocity or a position. */
    enum class JointMotorMode : std::uint8_t
    {
        Off,
        Velocity,
        Position
    };

    /** @brief Drives a hinge's angle or a slider's offset, measured from the rest pose. A hard limit always stops it.
     */
    struct JointMotor
    {
        JointMotorMode mode = JointMotorMode::Off;
        /** @brief Radians or metres per second, read in Velocity mode only. */
        float targetVelocity = 0.0f;
        /** @brief Radians or metres, read in Position mode only. Outside an enabled limit, it is taken as the limit. */
        float targetPosition = 0.0f;
        /** @brief Newton metres for a hinge, newtons for a slider, either way. Negative is treated as 0. */
        float maxForce = std::numeric_limits<float>::infinity();
        /** @brief How hard Position mode drives. */
        JointSpring spring = {.frequency = 2.0f, .damping = 1.0f};
    };

    /** @brief Drives a swing-twist joint's rotation, swing and twist together. Its cone and twist limits always stop
     * it. */
    struct SwingTwistMotor
    {
        JointMotorMode mode = JointMotorMode::Off;
        /** @brief frameB's rotation relative to frameA, read in Position mode only: the identity is the rest pose.
         * Outside the cone or the twist range, it is taken as the nearest rotation inside them. */
        Math::Quat targetRotation = Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
        /** @brief Radians per second in frameA's axes, read in Velocity mode only. */
        Math::Vec3 targetAngularVelocity = Math::Vec3(0.0f);
        /** @brief Newton metres, about each axis. Negative is treated as 0. */
        float maxTorque = std::numeric_limits<float>::infinity();
        /** @brief How hard Position mode drives. */
        JointSpring spring = {.frequency = 2.0f, .damping = 1.0f};
    };

    /** @brief Holds the two frames together in position and rotation: the bodies move as one. */
    struct FixedJoint
    {
    };

    /** @brief Holds the two frames' positions together and lets the bodies turn freely about them. */
    struct BallJoint
    {
    };

    /**
     * @brief Keeps the two frames' positions between `minDistance` and `maxDistance` apart, and ignores the frames'
     * rotations: a rod at min == max, a rope at min 0, a spring when soft.
     */
    struct DistanceJoint
    {
        /** @brief Metres. Negative is raised to 0, with a warning. */
        float minDistance = 0.0f;
        /** @brief Metres. Below `minDistance` is raised to it, with a warning. */
        float maxDistance = 1.0f;
        /** @brief Makes the distance soft: it stretches beyond the range and springs back. */
        JointSpring spring = {};
    };

    /**
     * @brief Holds the two frames together but for turning about their shared X axis. The hinge angle is signed by the
     * right-hand rule about frameA's X: it is frameB's turn relative to frameA, so it follows which body is A.
     */
    struct HingeJoint
    {
        /** @brief Radians. */
        JointLimits limits = {};
        JointMotor motor = {};
        /** @brief The most torque, in newton metres, resisting the hinge's turn while its motor is off: a door stays
         * where it is left. Negative is treated as 0. */
        float friction = 0.0f;
    };

    /** @brief Holds the two frames together but for sliding along their shared X axis, without turning. */
    struct SliderJoint
    {
        /** @brief Metres. */
        JointLimits limits = {};
        JointMotor motor = {};
        /** @brief The most force, in newtons, resisting the slide while its motor is off. Negative is treated as 0. */
        float friction = 0.0f;
    };

    /**
     * @brief Holds the two frames' positions together and keeps frameB's turn relative to frameA within hard limits, in
     * radians: a swing about Y and Z within a cone about frameA's X, and a twist about X within a range. A shoulder or
     * a hip.
     *
     * The swing limits are cone half-angles from 0 to a half turn (a half turn frees that swing), moved into that
     * range with a warning. The twist range lies within a half turn either way and takes in the rest pose, corrected
     * with a warning as JointLimits are.
     */
    struct SwingTwistJoint
    {
        /** @brief The cone's half-angle about Y. */
        float swingYLimit = std::numbers::pi_v<float> / 4.0f;
        /** @brief The cone's half-angle about Z. */
        float swingZLimit = std::numbers::pi_v<float> / 4.0f;
        float twistMin = -std::numbers::pi_v<float> / 4.0f;
        float twistMax = std::numbers::pi_v<float> / 4.0f;
        /** @brief The most torque, in newton metres, resisting the joint's turn while its motor is off. Negative is
         * treated as 0. */
        float friction = 0.0f;
        SwingTwistMotor motor = {};
    };

    /** @brief Which kind of joint a JointKind holds. */
    enum class JointType : std::uint8_t
    {
        Fixed,
        Ball,
        Distance,
        Hinge,
        Slider,
        SwingTwist
    };

    /**
     * @brief A joint kind: exactly one kind of joint and its settings, by value, as CollisionShape holds a shape.
     *
     * Assign a kind to change it (`joint.kind = HingeJoint{}`); read or edit its settings through the checked view
     * (`joint.kind.As<HingeJoint>()`). Reading it as a kind it does not hold asserts.
     */
    class JointKind
    {
      public:
        /** @brief A fixed joint (`FixedJoint{}`). */
        JointKind() : JointKind(FixedJoint{})
        {
        }

        /** @brief A fixed joint. Implicit, so a fixed joint can be assigned where a kind is wanted. */
        JointKind(FixedJoint fixed) : m_type(JointType::Fixed), m_fixed(fixed)
        {
        }

        /** @brief A ball joint. Implicit, so a ball joint can be assigned where a kind is wanted. */
        JointKind(BallJoint ball) : m_type(JointType::Ball), m_ball(ball)
        {
        }

        /** @brief A distance joint. Implicit, so a distance joint can be assigned where a kind is wanted. */
        JointKind(DistanceJoint distance) : m_type(JointType::Distance), m_distance(distance)
        {
        }

        /** @brief A hinge. Implicit, so a hinge can be assigned where a kind is wanted. */
        JointKind(HingeJoint hinge) : m_type(JointType::Hinge), m_hinge(hinge)
        {
        }

        /** @brief A slider. Implicit, so a slider can be assigned where a kind is wanted. */
        JointKind(SliderJoint slider) : m_type(JointType::Slider), m_slider(slider)
        {
        }

        /** @brief A swing-twist joint. Implicit, so a swing-twist joint can be assigned where a kind is wanted. */
        JointKind(SwingTwistJoint swingTwist) : m_type(JointType::SwingTwist), m_swingTwist(swingTwist)
        {
        }

        /** @brief The kind of joint held. */
        [[nodiscard]] JointType Type() const
        {
            return m_type;
        }

        /** @brief Whether the joint is of kind `T` (FixedJoint, BallJoint, DistanceJoint, HingeJoint, SliderJoint or
         * SwingTwistJoint). */
        template <typename T>
        [[nodiscard]] bool Is() const
        {
            return m_type == TypeOf<T>();
        }

        /** @brief The joint as kind `T`, to read or edit. Asserts that it is one (Is<T>()). */
        template <typename T>
        [[nodiscard]] T& As()
        {
            assert(Is<T>() && "JointKind::As: the joint is a different kind");
            return Member<T>(*this);
        }

        /** @copydoc As */
        template <typename T>
        [[nodiscard]] const T& As() const
        {
            assert(Is<T>() && "JointKind::As: the joint is a different kind");
            return Member<T>(*this);
        }

      private:
        template <typename T>
        static constexpr JointType TypeOf()
        {
            if constexpr (std::is_same_v<T, FixedJoint>)
            {
                return JointType::Fixed;
            }
            else if constexpr (std::is_same_v<T, BallJoint>)
            {
                return JointType::Ball;
            }
            else if constexpr (std::is_same_v<T, DistanceJoint>)
            {
                return JointType::Distance;
            }
            else if constexpr (std::is_same_v<T, HingeJoint>)
            {
                return JointType::Hinge;
            }
            else if constexpr (std::is_same_v<T, SliderJoint>)
            {
                return JointType::Slider;
            }
            else
            {
                static_assert(std::is_same_v<T, SwingTwistJoint>, "JointKind holds FixedJoint, BallJoint, "
                                                                  "DistanceJoint, HingeJoint, SliderJoint or "
                                                                  "SwingTwistJoint");
                return JointType::SwingTwist;
            }
        }

        template <typename T, typename Self>
        static auto& Member(Self& self)
        {
            if constexpr (TypeOf<T>() == JointType::Fixed)
            {
                return self.m_fixed;
            }
            else if constexpr (TypeOf<T>() == JointType::Ball)
            {
                return self.m_ball;
            }
            else if constexpr (TypeOf<T>() == JointType::Distance)
            {
                return self.m_distance;
            }
            else if constexpr (TypeOf<T>() == JointType::Hinge)
            {
                return self.m_hinge;
            }
            else if constexpr (TypeOf<T>() == JointType::Slider)
            {
                return self.m_slider;
            }
            else
            {
                return self.m_swingTwist;
            }
        }

        JointType m_type;
        union {
            FixedJoint m_fixed;
            BallJoint m_ball;
            DistanceJoint m_distance;
            HingeJoint m_hinge;
            SliderJoint m_slider;
            SwingTwistJoint m_swingTwist;
        };
    };

    /**
     * @brief Joins two bodies, or a body and the world (ADR-0013). Put it on a joint entity: any entity, one of the
     * bodies it joins included. Physics never reads the joint entity's Transform. A body takes part in any number of
     * joints, each on its own entity.
     *
     * The rest pose is where the two frames coincide in world space; physics never captures frames from the scene.
     * Use FrameFromWorld to author them: frames written by hand on a scaled or parented body are easy to get wrong.
     *
     * Plain data, which physics compares each tick with what it last applied (ADR-0011). `enabled`, `collideConnected`,
     * the break thresholds and every setting of the kind (limits, motors, springs, friction) are live edits, applied on
     * the next tick. Changing the kind, a body, a frame or a body's scale recreates the joint in the backend: cheap,
     * but one step softer while it settles. Every change wakes both bodies.
     *
     * Out-of-range settings are corrected as physics applies them, with one warning per joint for each kind of
     * mistake; the component keeps what was written.
     *
     * A joint is inactive while `bodyA` is empty, both bodies are the same entity, or either names something that is
     * not a body entity of the joint entity's scene (destroyed, never a body, a compound body's child collider, a
     * trigger, or in another scene). It does nothing, with one warning each time it goes inactive, wakes the body left,
     * and works again by itself once both names are bodies. A joint whose bodies are all static or kinematic is idle,
     * not inactive: kept silently, it holds once one becomes dynamic. Motion-type changes keep a joint as it is.
     *
     * Writing one body's Transform, or reparenting it, teleports it alone: the other body stays where it is, and the
     * joint pulls the two back together over the next steps, by position, so a teleport adds no velocity to them and
     * never breaks the joint (one left out of a resting pose, or past the joint's limits, still falls while it is
     * pulled back). How fast they come together is not promised. To move a jointed group as one, write each
     * of its bodies' Transforms in the same tick; there is no group teleport. A kinematic body's moves are not
     * teleports. Editing a body's colliders (their shapes, or the child colliders of a compound body added, removed or
     * moved) keeps its joints' frames where they are on it, wherever its centre of mass goes.
     */
    struct Joint
    {
        /** @brief A body entity: a RigidBody's, or a standalone static collider's. Never the world. */
        Entity bodyA;
        /** @brief A body entity, or empty: the world. */
        Entity bodyB;
        /** @brief Local to bodyA's origin. */
        JointFrame frameA;
        /** @brief Local to bodyB's origin, or in world space when bodyB is the world. */
        JointFrame frameB;
        JointKind kind;
        /** @brief A disabled joint holds nothing. */
        bool enabled = true;
        /**
         * @brief Whether the two bodies collide with each other. While the joint is enabled, false keeps every solid
         * collider of one from colliding with the other's; true leaves them to their CollisionFilters, which it never
         * overrides. Only the two bodies named: in a chain, bodies two apart still collide. Triggers and queries ignore
         * it. A live edit.
         */
        bool collideConnected = false;
        /**
         * @brief Newtons. The first step whose JointState::force is over it, while the joint's bodies are awake, breaks
         * the joint: physics sets `enabled` to false and sends a JointBroken. Infinity (the default) never breaks;
         * negative is treated as 0. Setting `enabled` back to true repairs it.
         */
        float breakForce = std::numeric_limits<float>::infinity();
        /** @brief Newton metres: as breakForce, against JointState::torque. */
        float breakTorque = std::numeric_limits<float>::infinity();
    };

    /** @brief Both frames of a joint, as FrameFromWorld computes them. */
    struct JointFrames
    {
        JointFrame frameA;
        JointFrame frameB;
    };

    /**
     * @brief The frames that put a joint's rest pose at `anchor` (world space), given where `bodyA` and `bodyB` are
     * now: `anchor` in each body's local space, through its world matrix. Only an empty `bodyB` is the world, whose
     * frame is `anchor` itself. Computed once, when called; it needs the scene, not a physics world.
     *
     * A body that is not alive (destroyed, or an empty `bodyA`) gives identity frames, with a warning in debug builds.
     */
    [[nodiscard]] JointFrames FrameFromWorld(const Entity& bodyA, const Entity& bodyB, const JointFrame& anchor);

    /**
     * @brief How a joint sits and how hard it pulls, as PhysicsWorld::GetJointState reads it from the last completed
     * step.
     *
     * The pose readings come from the bodies' poses after that step and the joint's frames, for every kind, never from
     * interpolated Transforms. `force` and `torque` sum every part of the joint (holding the frames together, its
     * limits, its motor and friction) over the step's last collision step: approximate within a step, exact enough for
     * steady loads such as a hanging chain's weight.
     */
    struct JointState
    {
        /** @brief Newtons, world space: what the joint applies to bodyB (bodyA gets the negative). */
        Math::Vec3 force = Math::Vec3(0.0f);
        /** @brief Newton metres, world space, about the joint's anchor: what the joint applies to bodyB, likewise. */
        Math::Vec3 torque = Math::Vec3(0.0f);
        /**
         * @brief frameB's pose in frameA's axes: the identity is the rest pose. For a swing-twist joint, its rotation
         * is in the same terms as its motor's `targetRotation`.
         */
        JointFrame relative;
        /**
         * @brief A hinge's angle (radians, wrapped to (-pi, pi], with no count of whole turns), a slider's offset along
         * frameA's X (metres), a distance joint's length (metres); 0 for the other kinds.
         *
         * A hinge's angle is frameB's turn relative to frameA, so its sign follows which body is A: put the fixed part
         * in bodyA to read a positive opening. A body hinged to the world (always bodyB) reads with the opposite sign.
         */
        float position = 0.0f;
    };
} // namespace Fenrir
