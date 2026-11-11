#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "Core/SceneHistory.hpp"
#include "Device/GraphicsSurface.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir::Render
{
    /**
     * @brief The rectangle a candidate's box covers on a view's screen (ADR-0057), as `builtin:cull`'s second phase
     * tests it against the view's Hi-Z pyramid: in texture coordinates, 0 to 1, x to the right and y down from the
     * top (the pyramid's texel rows), clamped to the screen, and the nearest reversed-Z depth of the box's corners.
     */
    struct ScreenRect
    {
        Math::Vec2 min{0.0f};
        Math::Vec2 max{0.0f};
        /** @brief The largest depth of the eight corners: the box's nearest point, under reversed-Z. */
        float nearest = 0.0f;
    };

    /**
     * @brief The screen rectangle of `box` under `transform` in a view of `viewProjection`, or none when a corner is
     * at or in front of the near plane (its clip w at most 0, or its depth past 1), which the test keeps: the box may
     * cover the camera, and its rectangle is not well defined.
     */
    [[nodiscard]] inline std::optional<ScreenRect> ScreenRectOf(const Math::AABB& box, const Math::Mat4& transform,
                                                                const Math::Mat4& viewProjection)
    {
        const Math::Mat4 clipFrom = viewProjection * transform;
        Math::Vec2 ndcMin(1e30f);
        Math::Vec2 ndcMax(-1e30f);
        float nearest = 0.0f;
        for (const Math::Vec3& corner : Math::Corners(box))
        {
            const Math::Vec4 clip = clipFrom * Math::Vec4(corner, 1.0f);
            if (clip.w <= 0.0f || clip.z > clip.w)
            {
                return std::nullopt;
            }
            const Math::Vec3 ndc = Math::Vec3(clip) / clip.w;
            ndcMin = glm::min(ndcMin, Math::Vec2(ndc));
            ndcMax = glm::max(ndcMax, Math::Vec2(ndc));
            nearest = std::max(nearest, ndc.z);
        }
        // Texture y runs down from the top, so the rectangle's top is the largest NDC y.
        ScreenRect rect;
        rect.min = glm::clamp(Math::Vec2(ndcMin.x * 0.5f + 0.5f, 0.5f - ndcMax.y * 0.5f), 0.0f, 1.0f);
        rect.max = glm::clamp(Math::Vec2(ndcMax.x * 0.5f + 0.5f, 0.5f - ndcMin.y * 0.5f), 0.0f, 1.0f);
        rect.nearest = nearest;
        return rect;
    }

    /**
     * @brief The level of a pyramid of `mips` levels over a `size` view whose texels cover `rect` in at most 2 x 2:
     * the level halving the rectangle's longer side, in the view's pixels, to at most one texel, the last level when
     * none does.
     */
    [[nodiscard]] inline std::uint32_t PyramidLevelOf(const ScreenRect& rect, Graphics::PixelSize size,
                                                      std::uint32_t mips)
    {
        const Math::Vec2 pixels =
            (rect.max - rect.min) * Math::Vec2(static_cast<float>(size.width), static_cast<float>(size.height));
        const float longest = std::max(std::max(pixels.x, pixels.y), 1.0f);
        const auto level = static_cast<std::uint32_t>(std::ceil(std::log2(longest)));
        return std::min(level, mips == 0 ? 0u : mips - 1);
    }

    /** @brief The texels of one pyramid level that a screen rectangle touches, inclusive. */
    struct TexelRange
    {
        glm::uvec2 lo{0};
        glm::uvec2 hi{0};
    };

    /**
     * @brief The texels of a level `levelSize` texels across that `rect` touches: from the texel holding its top-left
     * to the last texel its bottom-right reaches into, never past the level's edge.
     */
    [[nodiscard]] inline TexelRange TexelsOf(const ScreenRect& rect, Graphics::PixelSize levelSize)
    {
        const Math::Vec2 scale(static_cast<float>(levelSize.width), static_cast<float>(levelSize.height));
        const glm::uvec2 last(levelSize.width - 1, levelSize.height - 1);
        TexelRange range;
        range.lo = glm::min(glm::uvec2(glm::floor(rect.min * scale)), last);
        range.hi = glm::min(glm::uvec2(glm::max(glm::ceil(rect.max * scale) - 1.0f, Math::Vec2(range.lo))), last);
        return range;
    }

    /**
     * @brief Whether a candidate of screen rectangle `rect` is wholly behind what `level` (a pyramid level of
     * `levelSize` texels, top row first) holds: its nearest point is farther than the farthest depth of every texel
     * it touches. A texel holding nothing has a farthest depth of 0, which hides nothing.
     */
    [[nodiscard]] inline bool IsHidden(const ScreenRect& rect, std::span<const HiZTexel> level,
                                       Graphics::PixelSize levelSize)
    {
        const TexelRange texels = TexelsOf(rect, levelSize);
        float farthest = 1.0f;
        for (std::uint32_t y = texels.lo.y; y <= texels.hi.y; ++y)
        {
            for (std::uint32_t x = texels.lo.x; x <= texels.hi.x; ++x)
            {
                farthest = std::min(farthest, level[std::size_t{y} * levelSize.width + x].farthest);
            }
        }
        return rect.nearest < farthest;
    }
} // namespace Fenrir::Render
