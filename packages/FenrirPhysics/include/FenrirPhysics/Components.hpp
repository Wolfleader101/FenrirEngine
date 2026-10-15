#pragma once

// PROTOTYPE (physics spike): the physics components, plain data (ADR-0011). Only box and sphere shapes.

#include <cassert>
#include <cstdint>
#include <type_traits>

#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    enum class MotionType : std::uint8_t
    {
        Static,
        Kinematic,
        Dynamic
    };

    struct RigidBody
    {
        MotionType motionType = MotionType::Dynamic;
        float mass = 1.0f;
        Math::Vec3 linearVelocity = Math::Vec3(0.0f);
        Math::Vec3 angularVelocity = Math::Vec3(0.0f);
    };

    struct BoxShape
    {
        Math::Vec3 halfExtents = Math::Vec3(0.5f);
    };

    struct SphereShape
    {
        float radius = 0.5f;
    };

    enum class ShapeType : std::uint8_t
    {
        Box,
        Sphere
    };

    /** A tagged union: exactly one kind of shape, by value. */
    class CollisionShape
    {
      public:
        CollisionShape() : CollisionShape(BoxShape{})
        {
        }

        CollisionShape(BoxShape box) : m_type(ShapeType::Box), m_box(box)
        {
        }

        CollisionShape(SphereShape sphere) : m_type(ShapeType::Sphere), m_sphere(sphere)
        {
        }

        [[nodiscard]] ShapeType Type() const
        {
            return m_type;
        }

        template <typename T>
        [[nodiscard]] bool Is() const
        {
            return m_type == TypeOf<T>();
        }

        template <typename T>
        T& As()
        {
            assert(Is<T>());
            return *Member<T>(this);
        }

        template <typename T>
        const T& As() const
        {
            assert(Is<T>());
            return *Member<T>(const_cast<CollisionShape*>(this));
        }

        friend bool operator==(const CollisionShape& a, const CollisionShape& b)
        {
            if (a.m_type != b.m_type)
            {
                return false;
            }
            switch (a.m_type)
            {
            case ShapeType::Box:
                return a.m_box.halfExtents == b.m_box.halfExtents;
            case ShapeType::Sphere:
                return a.m_sphere.radius == b.m_sphere.radius;
            }
            return false;
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
                static_assert(std::is_same_v<T, SphereShape>);
                return ShapeType::Sphere;
            }
        }

        template <typename T>
        static T* Member(CollisionShape* shape)
        {
            if constexpr (std::is_same_v<T, BoxShape>)
            {
                return &shape->m_box;
            }
            else
            {
                return &shape->m_sphere;
            }
        }

        ShapeType m_type;
        union {
            BoxShape m_box;
            SphereShape m_sphere;
        };
    };

    struct Collider
    {
        CollisionShape shape;
    };
} // namespace Fenrir
