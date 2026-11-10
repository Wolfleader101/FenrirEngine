#pragma once

#include <cstdint>
#include <vector>

#include "Device/GraphicsSurface.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir::Render
{
    /** @brief The most ghosts a lens flare reads per pixel (LensFlare::ghostCount is held to it). */
    inline constexpr std::uint32_t MaxGhosts = 8;

    /**
     * @brief The CPU mirror of builtin:lens-flare's ghosts: where the pixel at `uv` (0 to 1 across the view, from its
     * top-left) reads its ghost `ghost` (1 for the first) at `spacing`. Each is `ghost` times `spacing` of the way from
     * the pixel towards the view's centre, so past the centre once that passes 1.
     */
    [[nodiscard]] Math::Vec2 GhostReadPoint(Math::Vec2 uv, std::uint32_t ghost, float spacing) noexcept;

    /**
     * @brief Where a bright point at `source` shows its first `count` ghosts at `spacing` (the count held to
     * MaxGhosts): the pixels that read it through GhostReadPoint, all on the line through it and the view's centre. A
     * ghost read exactly at the centre is read there by every pixel, so it fills the view and has no place: it is left
     * out.
     */
    [[nodiscard]] std::vector<Math::Vec2> GhostPositions(Math::Vec2 source, std::uint32_t count, float spacing);

    /**
     * @brief The CPU mirror of a bloom or flare composite's DIRT variant: a pixel's `share` of the bloom or the flare
     * under lens dirt of brightness `dirt` (0 to 1): multiplied by 1 + `dirtIntensity` times it, and held at or below
     * 1, so the mix stays a blend. A dirt intensity or dirt of 0 or less keeps the share.
     */
    [[nodiscard]] float LensDirtShare(float share, float dirtIntensity, float dirt) noexcept;

    /**
     * @brief Which level of the bloom chain of a view of `size` the flare reads: level 1 (a quarter of the view's size)
     * when the chain has it, else level 0.
     */
    [[nodiscard]] std::uint32_t FlareLevel(Graphics::PixelSize size) noexcept;
} // namespace Fenrir::Render
