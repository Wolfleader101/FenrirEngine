#pragma once

// How the diff compares a component with what physics last applied: bit for bit, so any value game code writes is an
// edit, and a value physics wrote back is not. Private to the FenrirPhysics package.

#include <bit>
#include <cstdint>

#include "FenrirMath/Math.hpp"

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
} // namespace Fenrir
