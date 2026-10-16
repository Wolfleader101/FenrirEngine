#pragma once

// How a box, sphere or capsule takes its entity's scale (as Components.hpp documents it): the dimensions the backend
// shape is made with, and that debug lines draw. Private; no backend type.

#include <algorithm>
#include <bit>
#include <cstdint>
#include <tuple>

#include "FenrirMath/Math.hpp"
#include "FenrirPhysics/Components.hpp"

namespace Fenrir::Backend
{
    // No dimension of a box, sphere or capsule is made smaller than this, after scale: the backend asserts on empty
    // shapes, and anything thinner than a millimetre is not simulated well anyway.
    inline constexpr float minimumSize = 1.0e-3f;

    // A box, sphere or capsule with the entity's scale baked into its dimensions: what the backend shape is made from,
    // and the key of the shape cache. Box: half extents. Sphere: radius in x. Capsule: half height in x, radius in y.
    // Unused dimensions are zero.
    struct Primitive
    {
        ShapeType type;
        Math::Vec3 size;

        // Ordered bit for bit, like the diff: equal keys make equal shapes.
        friend bool operator<(const Primitive& a, const Primitive& b)
        {
            const auto key = [](const Primitive& p) {
                return std::tuple(p.type, std::bit_cast<std::uint32_t>(p.size.x),
                                  std::bit_cast<std::uint32_t>(p.size.y), std::bit_cast<std::uint32_t>(p.size.z));
            };
            return key(a) < key(b);
        }
    };

    // Mirroring is ignored (a primitive is symmetric) and no dimension is below minimumSize. `shape` must be a box,
    // sphere or capsule.
    inline Primitive Bake(const CollisionShape& shape, const Math::Vec3& scale)
    {
        const Math::Vec3 size = glm::abs(scale);
        if (shape.Is<SphereShape>())
        {
            // The sphere takes only a uniform scale: the largest axis wins.
            const float radius = shape.As<SphereShape>().radius * std::max({size.x, size.y, size.z});
            return Primitive{ShapeType::Sphere, Math::Vec3(std::max(radius, minimumSize), 0.0f, 0.0f)};
        }
        if (shape.Is<CapsuleShape>())
        {
            // The capsule takes only a uniform scale: its height follows y, its radius the larger of x and z.
            const CapsuleShape& capsule = shape.As<CapsuleShape>();
            return Primitive{ShapeType::Capsule,
                             Math::Vec3(std::max(capsule.halfHeight * size.y, minimumSize),
                                        std::max(capsule.radius * std::max(size.x, size.z), minimumSize), 0.0f)};
        }
        return Primitive{ShapeType::Box, glm::max(shape.As<BoxShape>().halfExtents * size, Math::Vec3(minimumSize))};
    }
} // namespace Fenrir::Backend
