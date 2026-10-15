#pragma once

#include <cassert>
#include <cstdint>
#include <type_traits>

#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    /** @brief How a rigid body moves: never (static), by game code (kinematic) or by the simulation (dynamic). */
    enum class MotionType : std::uint8_t
    {
        Static,
        Kinematic,
        Dynamic
    };

    /**
     * @brief Makes the entity's collider a rigid body the physics world simulates (ADR-0011). Plain data: physics finds
     * edits by comparing it, each tick, with what it last applied.
     *
     * A collider without a RigidBody is static, as is `RigidBody{MotionType::Static}`. Switching `motionType` is a
     * field edit and takes effect on the next tick. Physics writes a dynamic body's velocities back after each step;
     * writing them sets the body's velocity. A kinematic body is moved by writing its `Transform` (in `Tick`, so
     * physics sees it in `PostTick`); it pushes dynamic bodies and is not pushed.
     */
    struct RigidBody
    {
        MotionType motionType = MotionType::Dynamic;
        /** @brief Kilograms. Used by dynamic bodies. */
        float mass = 1.0f;
        /** @brief How fast linear velocity decays on its own, per second. */
        float linearDamping = 0.05f;
        /** @brief How fast angular velocity decays on its own, per second. */
        float angularDamping = 0.05f;
        /** @brief Multiplies the world's gravity for this body. */
        float gravityScale = 1.0f;
        /** @brief Metres per second, in world space. */
        Math::Vec3 linearVelocity = Math::Vec3(0.0f);
        /** @brief Radians per second about each world axis. */
        Math::Vec3 angularVelocity = Math::Vec3(0.0f);
    };

    /** @brief A box centred on its entity, `halfExtents` from the centre to each face before the entity's scale. */
    struct BoxShape
    {
        Math::Vec3 halfExtents = Math::Vec3(0.5f);
    };

    /** @brief A sphere centred on its entity. A non-uniform scale is replaced by its largest component. */
    struct SphereShape
    {
        float radius = 0.5f;
    };

    /** @brief Which kind of shape a CollisionShape holds. */
    enum class ShapeType : std::uint8_t
    {
        Box,
        Sphere
    };

    /**
     * @brief A collision shape: exactly one kind of shape and its dimensions, by value (ADR-0011).
     *
     * Assign a kind to change it (`collider.shape = SphereShape{1.0f}`); read or edit the dimensions through the
     * checked view (`collider.shape.As<BoxShape>().halfExtents.y = 2.0f`). Reading it as a kind it does not hold
     * asserts.
     */
    class CollisionShape
    {
      public:
        /** @brief A unit box (`BoxShape{}`). */
        CollisionShape() : CollisionShape(BoxShape{})
        {
        }

        /** @brief A box. Implicit, so a box can be assigned where a shape is wanted. */
        CollisionShape(BoxShape box) : m_type(ShapeType::Box), m_box(box)
        {
        }

        /** @brief A sphere. Implicit, so a sphere can be assigned where a shape is wanted. */
        CollisionShape(SphereShape sphere) : m_type(ShapeType::Sphere), m_sphere(sphere)
        {
        }

        /** @brief The kind of shape held. */
        [[nodiscard]] ShapeType Type() const
        {
            return m_type;
        }

        /** @brief Whether the shape is of kind `T` (BoxShape, SphereShape). */
        template <typename T>
        [[nodiscard]] bool Is() const
        {
            return m_type == TypeOf<T>();
        }

        /** @brief The shape as kind `T`, to read or edit. Asserts that it is one (Is<T>()). */
        template <typename T>
        [[nodiscard]] T& As()
        {
            assert(Is<T>() && "CollisionShape::As: the shape is a different kind");
            return Member<T>(*this);
        }

        /** @copydoc As */
        template <typename T>
        [[nodiscard]] const T& As() const
        {
            assert(Is<T>() && "CollisionShape::As: the shape is a different kind");
            return Member<T>(*this);
        }

      private:
        template <typename T>
        static constexpr ShapeType TypeOf()
        {
            if constexpr (std::is_same_v<T, BoxShape>)
            {
                return ShapeType::Box;
            }
            else
            {
                static_assert(std::is_same_v<T, SphereShape>, "CollisionShape holds BoxShape or SphereShape");
                return ShapeType::Sphere;
            }
        }

        template <typename T, typename Self>
        static auto& Member(Self& self)
        {
            if constexpr (TypeOf<T>() == ShapeType::Box)
            {
                return self.m_box;
            }
            else
            {
                return self.m_sphere;
            }
        }

        ShapeType m_type;
        union {
            BoxShape m_box;
            SphereShape m_sphere;
        };
    };

    /**
     * @brief Gives the entity collision geometry, placed at its world transform and scaled by its world scale. Without
     * a RigidBody the collider is static.
     */
    struct Collider
    {
        CollisionShape shape;
    };
} // namespace Fenrir
