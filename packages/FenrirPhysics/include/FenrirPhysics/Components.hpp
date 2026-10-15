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

    /**
     * @brief A box centred on its entity, `halfExtents` from the centre to each face before the entity's scale, which
     * applies per axis. Its edges are rounded slightly (by at most 0.05, less for a thin box).
     */
    struct BoxShape
    {
        Math::Vec3 halfExtents = Math::Vec3(0.5f);
    };

    /** @brief A sphere centred on its entity. A non-uniform scale is replaced by its largest component. */
    struct SphereShape
    {
        float radius = 0.5f;
    };

    /**
     * @brief A capsule centred on its entity and standing along its local y axis: a cylinder `2 * halfHeight` tall,
     * capped by hemispheres of `radius`, so `2 * (halfHeight + radius)` tall in all. Scale stretches `halfHeight` by
     * the entity's y scale and `radius` by the larger of its x and z scales.
     */
    struct CapsuleShape
    {
        float halfHeight = 0.5f;
        float radius = 0.5f;
    };

    /**
     * @brief A handle to a collision mesh: vertex and index data FenrirPhysics cooked once
     * (Physics::CreateCollisionMesh) and colliders share. Plain data; copying it does not keep the mesh alive. Valid
     * until released (Physics::ReleaseCollisionMesh); a released handle is never handed out again. The default handle
     * refers to no mesh.
     */
    struct CollisionMesh
    {
        std::uint64_t id = 0;

        friend bool operator==(const CollisionMesh&, const CollisionMesh&) = default;
    };

    /**
     * @brief The convex hull of a collision mesh's vertices (its indices are not used), placed on its entity as the
     * vertices are placed about the origin. Scale is applied per axis, mirroring included.
     */
    struct ConvexHullShape
    {
        CollisionMesh hull;
    };

    /**
     * @brief A collision mesh's triangles, as they are placed about the origin. Scale is applied per axis, mirroring
     * included. Triangles collide on their front face only (counter-clockwise when seen from in front).
     *
     * A triangle mesh has no volume: on a dynamic body it is weighed as the solid box of its bounds, and two triangle
     * meshes never collide with each other. Prefer it for static level geometry, and a hull for anything that moves.
     */
    struct MeshShape
    {
        CollisionMesh mesh;
    };

    /** @brief Which kind of shape a CollisionShape holds. */
    enum class ShapeType : std::uint8_t
    {
        Box,
        Sphere,
        Capsule,
        ConvexHull,
        Mesh
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

        /** @brief A capsule. Implicit, so a capsule can be assigned where a shape is wanted. */
        CollisionShape(CapsuleShape capsule) : m_type(ShapeType::Capsule), m_capsule(capsule)
        {
        }

        /** @brief A convex hull. Implicit, so a hull can be assigned where a shape is wanted. */
        CollisionShape(ConvexHullShape hull) : m_type(ShapeType::ConvexHull), m_hull(hull)
        {
        }

        /** @brief A triangle mesh. Implicit, so a mesh can be assigned where a shape is wanted. */
        CollisionShape(MeshShape mesh) : m_type(ShapeType::Mesh), m_mesh(mesh)
        {
        }

        /** @brief The kind of shape held. */
        [[nodiscard]] ShapeType Type() const
        {
            return m_type;
        }

        /** @brief Whether the shape is of kind `T` (BoxShape, SphereShape, CapsuleShape, ConvexHullShape, MeshShape).
         */
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
            else if constexpr (std::is_same_v<T, SphereShape>)
            {
                return ShapeType::Sphere;
            }
            else if constexpr (std::is_same_v<T, CapsuleShape>)
            {
                return ShapeType::Capsule;
            }
            else if constexpr (std::is_same_v<T, ConvexHullShape>)
            {
                return ShapeType::ConvexHull;
            }
            else
            {
                static_assert(std::is_same_v<T, MeshShape>, "CollisionShape holds BoxShape, SphereShape, CapsuleShape, "
                                                            "ConvexHullShape or MeshShape");
                return ShapeType::Mesh;
            }
        }

        template <typename T, typename Self>
        static auto& Member(Self& self)
        {
            if constexpr (TypeOf<T>() == ShapeType::Box)
            {
                return self.m_box;
            }
            else if constexpr (TypeOf<T>() == ShapeType::Sphere)
            {
                return self.m_sphere;
            }
            else if constexpr (TypeOf<T>() == ShapeType::Capsule)
            {
                return self.m_capsule;
            }
            else if constexpr (TypeOf<T>() == ShapeType::ConvexHull)
            {
                return self.m_hull;
            }
            else
            {
                return self.m_mesh;
            }
        }

        ShapeType m_type;
        union {
            BoxShape m_box;
            SphereShape m_sphere;
            CapsuleShape m_capsule;
            ConvexHullShape m_hull;
            MeshShape m_mesh;
        };
    };

    /**
     * @brief Gives the entity collision geometry, placed at its world transform and scaled by its world scale. Without
     * a RigidBody the collider is static.
     *
     * The scale is baked into the backend's shape, so changing the shape's dimensions or the entity's scale rebuilds
     * that shape on the next tick: fine occasionally, costly every tick. Each kind's shape documents how it takes a
     * non-uniform scale. A box, sphere or capsule is never thinner than 0.001 in any dimension once scaled (a zero size
     * is clamped to that). Colliders whose box, sphere or capsule is identical once scaled share one backend shape.
     */
    struct Collider
    {
        CollisionShape shape;
    };
} // namespace Fenrir
