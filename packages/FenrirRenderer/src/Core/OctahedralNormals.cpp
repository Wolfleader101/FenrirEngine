#include "Core/OctahedralNormals.hpp"

#include <cmath>

namespace Fenrir::Render
{
    namespace
    {
        /** @brief -1 for a negative component, else 1, as FenrirSignNotZero. */
        Math::Vec2 SignNotZero(Math::Vec2 v) noexcept
        {
            return {v.x >= 0.0f ? 1.0f : -1.0f, v.y >= 0.0f ? 1.0f : -1.0f};
        }
    } // namespace

    Math::Vec2 OctahedralEncode(Math::Vec3 n) noexcept
    {
        const Math::Vec2 p = Math::Vec2(n.x, n.y) / (std::abs(n.x) + std::abs(n.y) + std::abs(n.z));
        if (n.z >= 0.0f)
        {
            return p;
        }
        return (Math::Vec2(1.0f) - Math::Vec2(std::abs(p.y), std::abs(p.x))) * SignNotZero(p);
    }

    Math::Vec3 OctahedralDecode(Math::Vec2 e) noexcept
    {
        Math::Vec3 n(e.x, e.y, 1.0f - std::abs(e.x) - std::abs(e.y));
        if (n.z < 0.0f)
        {
            const Math::Vec2 folded =
                (Math::Vec2(1.0f) - Math::Vec2(std::abs(n.y), std::abs(n.x))) * SignNotZero(Math::Vec2(n.x, n.y));
            n.x = folded.x;
            n.y = folded.y;
        }
        return glm::normalize(n);
    }
} // namespace Fenrir::Render
