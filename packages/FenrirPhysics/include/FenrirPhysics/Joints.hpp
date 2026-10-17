#pragma once

// PROTOTYPE (joints spike, ticket 27): only the joint kinds a hinged door and a hanging chain need (BallJoint,
// HingeJoint), through the model of ADR-0013 and tickets 23 to 26. Never merged.

#include <cassert>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    /** @brief A pose local to a body entity's origin (scaled with it), or in world space for the world end. Its X
     * axis is the joint axis. */
    struct JointFrame
    {
        Math::Vec3 position = Math::Vec3(0.0f);
        Math::Quat rotation = Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
    };

    /** @brief A soft constraint: `frequency` in Hz (0 is rigid), `damping` as a ratio (1 is critical). */
    struct JointSpring
    {
        float frequency = 0.0f;
        float damping = 0.0f;
    };

    /** @brief A range about the rest pose (radians for a hinge). Disabled means free; `min <= 0 <= max`. */
    struct JointLimits
    {
        bool enabled = false;
        float min = 0.0f;
        float max = 0.0f;
        JointSpring spring;
    };

    enum class JointMotorMode : std::uint8_t
    {
        Off,
        Velocity,
        Position
    };

    /** @brief Drives a hinge's angle (Position) or angular velocity (Velocity), with at most `maxForce` N·m. */
    struct JointMotor
    {
        JointMotorMode mode = JointMotorMode::Off;
        float targetVelocity = 0.0f;
        float targetPosition = 0.0f;
        float maxForce = std::numeric_limits<float>::infinity();
        JointSpring spring = {2.0f, 1.0f};
    };

    /** @brief The two frames' positions held together; rotation free. */
    struct BallJoint
    {
    };

    /** @brief Rotation about the frames' shared X axis only. */
    struct HingeJoint
    {
        JointLimits limits;
        JointMotor motor;
        /** @brief N·m resisting rotation. */
        float friction = 0.0f;
    };

    enum class JointType : std::uint8_t
    {
        Ball,
        Hinge
    };

    /** @brief One joint kind and its settings, by value, as CollisionShape holds a shape. */
    class JointKind
    {
      public:
        JointKind() : JointKind(BallJoint{})
        {
        }

        JointKind(BallJoint ball) : m_type(JointType::Ball), m_ball(ball)
        {
        }

        JointKind(HingeJoint hinge) : m_type(JointType::Hinge), m_hinge(hinge)
        {
        }

        [[nodiscard]] JointType Type() const
        {
            return m_type;
        }

        template <typename T>
        [[nodiscard]] bool Is() const
        {
            return m_type == TypeOf<T>();
        }

        template <typename T>
        [[nodiscard]] T& As()
        {
            assert(Is<T>() && "JointKind::As: the joint is a different kind");
            return Member<T>(*this);
        }

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
            if constexpr (std::is_same_v<T, BallJoint>)
            {
                return JointType::Ball;
            }
            else
            {
                static_assert(std::is_same_v<T, HingeJoint>, "JointKind holds BallJoint or HingeJoint");
                return JointType::Hinge;
            }
        }

        template <typename T, typename Self>
        static auto& Member(Self& self)
        {
            if constexpr (TypeOf<T>() == JointType::Ball)
            {
                return self.m_ball;
            }
            else
            {
                return self.m_hinge;
            }
        }

        JointType m_type;
        union {
            BallJoint m_ball;
            HingeJoint m_hinge;
        };
    };

    /**
     * @brief A joint between two bodies, on a joint entity (any entity; its Transform is ignored). The rest pose is
     * the two frames coinciding in world space. A joint naming no valid body is inactive until both are valid.
     */
    struct Joint
    {
        /** @brief A body entity: a RigidBody's, or a standalone static collider's. Never empty. */
        Entity bodyA;
        /** @brief A body entity, or empty: the world. */
        Entity bodyB;
        JointFrame frameA;
        JointFrame frameB;
        JointKind kind;
        bool enabled = true;
        /** @brief Whether the two bodies collide with each other. */
        bool collideConnected = false;
        /** @brief N; one step over disables the joint and sends JointBroken. */
        float breakForce = std::numeric_limits<float>::infinity();
        /** @brief N·m; as breakForce. */
        float breakTorque = std::numeric_limits<float>::infinity();
    };

    /** @brief A joint's state as the last step left it (PhysicsWorld::JointState). */
    struct JointState
    {
        /** @brief N, world space, on bodyB (bodyA gets the negative). */
        Math::Vec3 force = Math::Vec3(0.0f);
        /** @brief N·m, likewise. */
        Math::Vec3 torque = Math::Vec3(0.0f);
        /** @brief frameB's pose in frameA's axes; identity is the rest pose. */
        JointFrame relative;
        /** @brief Hinge angle (rad, in (-π, π]); 0 for a ball. */
        float position = 0.0f;
    };

    /** @brief Both frames of a joint from one world pose: what FrameFromWorld gives. */
    struct JointFrames
    {
        JointFrame frameA;
        JointFrame frameB;
    };

    /**
     * @brief The frames that put a joint's anchor at `anchor` (world space) given where `bodyA` and `bodyB` are now. An
     * empty `bodyB` is the world. Computed once, when called.
     */
    [[nodiscard]] JointFrames FrameFromWorld(const Entity& bodyA, const Entity& bodyB, const JointFrame& anchor);
} // namespace Fenrir
