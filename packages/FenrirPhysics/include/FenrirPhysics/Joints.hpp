#pragma once

#include <cassert>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    /**
     * @brief A pose on a body: local to the body entity's origin, or in world space for the world end of a joint. Its
     * position follows the body's world scale (an anchor on a crate's corner stays on the corner); its rotation does
     * not. Its local X axis is a hinge's axis.
     */
    struct JointFrame
    {
        Math::Vec3 position = Math::Vec3(0.0f);
        Math::Quat rotation = Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
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
     * @brief Holds the two frames together but for turning about their shared X axis. The hinge angle is signed by the
     * right-hand rule about frameA's X: it is frameB's turn relative to frameA, so it follows which body is A.
     */
    struct HingeJoint
    {
    };

    /** @brief Which kind of joint a JointKind holds. */
    enum class JointType : std::uint8_t
    {
        Fixed,
        Ball,
        Hinge
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

        /** @brief A hinge. Implicit, so a hinge can be assigned where a kind is wanted. */
        JointKind(HingeJoint hinge) : m_type(JointType::Hinge), m_hinge(hinge)
        {
        }

        /** @brief The kind of joint held. */
        [[nodiscard]] JointType Type() const
        {
            return m_type;
        }

        /** @brief Whether the joint is of kind `T` (FixedJoint, BallJoint, HingeJoint). */
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
            else
            {
                static_assert(std::is_same_v<T, HingeJoint>, "JointKind holds FixedJoint, BallJoint or HingeJoint");
                return JointType::Hinge;
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
            else
            {
                return self.m_hinge;
            }
        }

        JointType m_type;
        union {
            FixedJoint m_fixed;
            BallJoint m_ball;
            HingeJoint m_hinge;
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
     * Plain data, which physics compares each tick with what it last applied (ADR-0011). `enabled` is a live edit,
     * applied on the next tick. Changing the kind, a body, a frame or a body's scale recreates the joint in the
     * backend: cheap, but one step softer while it settles. Every change wakes both bodies.
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
