#include "Core/LensFlare.hpp"

#include <algorithm>
#include <cmath>

#include "Core/Bloom.hpp"

namespace Fenrir::Render
{
    Math::Vec2 GhostReadPoint(Math::Vec2 uv, std::uint32_t ghost, float spacing) noexcept
    {
        const float along = static_cast<float>(ghost) * spacing;
        return uv + (Math::Vec2(0.5f) - uv) * along;
    }

    std::vector<Math::Vec2> GhostPositions(Math::Vec2 source, std::uint32_t count, float spacing)
    {
        // The pixel q reads ghost i at 0.5 + (q - 0.5)(1 - i spacing), which is `source` where
        // q = 0.5 + (source - 0.5) / (1 - i spacing).
        std::vector<Math::Vec2> positions;
        for (std::uint32_t ghost = 1; ghost <= std::min(count, MaxGhosts); ++ghost)
        {
            const float scale = 1.0f - static_cast<float>(ghost) * spacing;
            if (std::abs(scale) < 1e-6f)
            {
                continue;
            }
            positions.push_back(Math::Vec2(0.5f) + (source - Math::Vec2(0.5f)) / scale);
        }
        return positions;
    }

    float LensDirtShare(float share, float dirtIntensity, float dirt) noexcept
    {
        return std::min(share * (1.0f + std::max(dirtIntensity, 0.0f) * std::max(dirt, 0.0f)), 1.0f);
    }

    std::uint32_t FlareLevel(Graphics::PixelSize size) noexcept
    {
        return BloomLevels(size) > 1 ? 1 : 0;
    }
} // namespace Fenrir::Render
