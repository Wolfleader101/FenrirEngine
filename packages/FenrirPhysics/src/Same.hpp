#pragma once

// How the diff compares a component with what physics last applied: bit for bit, so any value game code writes is an
// edit, and a value physics wrote back is not. Private to the FenrirPhysics package.

#include <bit>
#include <cstdint>

#include "FenrirMath/Math.hpp"
#include "FenrirPhysics/Components.hpp"

namespace Fenrir
{
    [[nodiscard]] inline bool Same(float a, float b)
    {
        return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
    }

    [[nodiscard]] inline bool Same(const Math::Vec3& a, const Math::Vec3& b)
    {
        return Same(a.x, b.x) && Same(a.y, b.y) && Same(a.z, b.z);
    }

    [[nodiscard]] inline bool Same(const Math::Quat& a, const Math::Quat& b)
    {
        return Same(a.x, b.x) && Same(a.y, b.y) && Same(a.z, b.z) && Same(a.w, b.w);
    }

    [[nodiscard]] inline bool Same(const CollisionShape& a, const CollisionShape& b)
    {
        if (a.Type() != b.Type())
        {
            return false;
        }
        if (a.Is<SphereShape>())
        {
            return Same(a.As<SphereShape>().radius, b.As<SphereShape>().radius);
        }
        if (a.Is<ConvexHullShape>())
        {
            return a.As<ConvexHullShape>().hull == b.As<ConvexHullShape>().hull;
        }
        if (a.Is<MeshShape>())
        {
            return a.As<MeshShape>().mesh == b.As<MeshShape>().mesh;
        }
        if (a.Is<CapsuleShape>())
        {
            const CapsuleShape& x = a.As<CapsuleShape>();
            const CapsuleShape& y = b.As<CapsuleShape>();
            return Same(x.halfHeight, y.halfHeight) && Same(x.radius, y.radius);
        }
        return Same(a.As<BoxShape>().halfExtents, b.As<BoxShape>().halfExtents);
    }

    [[nodiscard]] inline bool Same(const PhysicsMaterial& a, const PhysicsMaterial& b)
    {
        return Same(a.friction, b.friction) && Same(a.restitution, b.restitution);
    }

    [[nodiscard]] inline bool Same(const CollisionFilter& a, const CollisionFilter& b)
    {
        return a.layers == b.layers && a.mask == b.mask;
    }
} // namespace Fenrir
