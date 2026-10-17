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
        /** @brief Hertz: 0 is rigid. */
        float frequency = 0.0f;
        /** @brief The damping ratio: 1 is critical. */
        float damping = 1.0f;
    };

    /** @brief The range a hinge or slider is kept in, measured from the rest pose. Disabled, it is free. */
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

    /** @brief Drives a hinge's angle or a slider's offset, measured from the rest pose. */
    struct JointMotor
    {
        JointMotorMode mode = JointMotorMode::Off;
        /** @brief Radians or metres per second, read in Velocity mode only. */
        float targetVelocity = 0.0f;
        /** @brief Radians or metres, read in Position mode only. */
        float targetPosition = 0.0f;
        /** @brief Newton metres for a hinge, newtons for a slider, either way. */
        float maxForce = std::numeric_limits<float>::infinity();
        /** @brief How hard Position mode drives. */
        JointSpring spring = {.frequency = 2.0f, .damping = 1.0f};
    };

    /** @brief Drives a swing-twist joint's rotation. */
    struct SwingTwistMotor
    {
        JointMotorMode mode = JointMotorMode::Off;
        /** @brief frameB's rotation relative to frameA, read in Position mode only: the identity is the rest pose. */
        Math::Quat targetRotation = Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
        /** @brief Radians per second in frameA's axes, read in Velocity mode only. */
        Math::Vec3 targetAngularVelocity = Math::Vec3(0.0f);
        /** @brief Newton metres. */
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
     * rotations: a rod at min == max, a rope at min 0.
     */
    struct DistanceJoint
    {
        /** @brief Metres. */
        float minDistance = 0.0f;
        /** @brief Metres. */
        float maxDistance = 1.0f;
        /** @brief Makes the distance soft. Not yet applied. */
        JointSpring spring = {};
    };

    /**
     * @brief Holds the two frames together but for turning about their shared X axis. The hinge angle is signed by the
     * right-hand rule about frameA's X: it is frameB's turn relative to frameA, so it follows which body is A.
     */
    struct HingeJoint
    {
        /** @brief Not yet applied. */
        JointLimits limits = {};
        /** @brief Not yet applied. */
        JointMotor motor = {};
        /** @brief Newton metres. Not yet applied. */
        float friction = 0.0f;
    };

    /** @brief Holds the two frames together but for sliding along their shared X axis, without turning. */
    struct SliderJoint
    {
        /** @brief Not yet applied. */
        JointLimits limits = {};
        /** @brief Not yet applied. */
        JointMotor motor = {};
        /** @brief Newtons. Not yet applied. */
        float friction = 0.0f;
    };

    /**
     * @brief Holds the two frames' positions together and keeps frameB's turn relative to frameA within hard limits, in
     * radians: a swing about Y and Z within a cone about frameA's X, and a twist about X within a range. A shoulder or
     * a hip.
     */
    struct SwingTwistJoint
    {
        /** @brief The cone's half-angle about Y. */
        float swingYLimit = std::numbers::pi_v<float> / 4.0f;
        /** @brief The cone's half-angle about Z. */
        float swingZLimit = std::numbers::pi_v<float> / 4.0f;
        float twistMin = -std::numbers::pi_v<float> / 4.0f;
        float twistMax = std::numbers::pi_v<float> / 4.0f;
        /** @brief Newton metres. Not yet applied. */
        float friction = 0.0f;
        /** @brief Not yet applied. */
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
     * Plain data, which physics compares each tick with what it last applied (ADR-0011). `enabled` and a distance
     * joint's range are live edits, applied on the next tick. Changing the kind, a body, a frame or a body's scale
     * recreates the joint in the backend: cheap, but one step softer while it settles. Every change wakes both bodies.
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
        /** @brief Whether the two bodies collide with each other. Not yet applied. */
        bool collideConnected = false;
        /** @brief Newtons. Not yet applied. */
        float breakForce = std::numeric_limits<float>::infinity();
        /** @brief Newton metres. Not yet applied. */
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
} // namespace Fenrir
