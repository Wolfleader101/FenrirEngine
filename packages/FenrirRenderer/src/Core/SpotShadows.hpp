#pragma once

#include <cstdint>
#include <span>

#include "Core/LightShadows.hpp"
#include "Core/PunctualLights.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir::Render
{
    // Spot shadows (ADR-0050): a budget of shadowed spot lights, the nearest to the views each frame, each a tile of
    // one depth atlas pooled by the budget, with its own perspective projection; read as the sun's cascades are, by
    // `Fenrir/Shadows.glsl`.

    /** @brief RendererConfig's default spot shadow budget and tile side. */
    inline constexpr ShadowBudgetConfig DefaultSpotShadows{8, 1024};

    /** @brief The atlas's tiles across and down: as square as the budget allows, `columns` x `rows` >= budget. */
    struct SpotAtlasGrid
    {
        /** @brief Tiles across. */
        std::uint32_t columns = 0;
        /** @brief Tiles down. */
        std::uint32_t rows = 0;
    };

    /** @brief The grid of `budget` tiles: ceil(sqrt(budget)) across, as many rows as that needs; 0 x 0 for none. */
    [[nodiscard]] SpotAtlasGrid SpotAtlasGridOf(std::uint32_t budget) noexcept;

    /** @brief Where a tile sits in its grid, counted from the top left. */
    struct SpotAtlasTile
    {
        std::uint32_t column = 0;
        std::uint32_t row = 0;
    };

    /** @brief Tile `tile` of `grid`, in order along the rows from the top left. */
    [[nodiscard]] SpotAtlasTile SpotAtlasTileOf(std::uint32_t tile, SpotAtlasGrid grid) noexcept;

    /** @brief The spot lights that keep their shadows: ChooseShadowedLights of the spot lights. */
    [[nodiscard]] ShadowChoice ChooseSpotShadows(std::span<const PunctualLight> lights,
                                                 std::span<const Math::Vec3> cameras, std::uint32_t budget);

    /** @brief A spot light's shadow: what its tile's shadow pass draws with. */
    struct SpotShadow
    {
        /** @brief World to the light's view, looking along the light from its position. */
        Math::Mat4 view{1.0f};
        /** @brief The light's view to its clip space: a square reversed-Z perspective, its near plane `near`. */
        Math::Mat4 projection{1.0f};
        /** @brief World to the light's clip space: what its tile's shadow pass's view block holds. */
        Math::Mat4 viewProjection{1.0f};
        /** @brief The world size of one of its texels one unit along the light's axis: it grows with the distance. */
        float texelScale = 0.0f;
        /** @brief Its near plane's distance from the light. */
        float near = 0.0f;
    };

    /** @brief The steepest outer angle a spot shadow's projection follows, in radians: 80 degrees. */
    inline constexpr float MaxSpotShadowAngle = 1.3962634f;

    /**
     * @brief The shadow of the spot `light`, drawn into a tile of `mapSize` texels: a square perspective from its
     * position along its direction, wide enough to hold its outer cone (held to MaxSpotShadowAngle) with the filter's
     * reach and a texel to spare on each side, its near plane a two-hundredth of its range, at most 0.1.
     */
    [[nodiscard]] SpotShadow FitSpotShadow(const PunctualLight& light, std::uint32_t mapSize);

    /**
     * @brief World to the spot shadow atlas for tile `tile` of `grid`, in tile order along the rows from the top left:
     * x and y over w are u and v (from the top) in the tile, and z over w its reversed-Z depth.
     */
    [[nodiscard]] Math::Mat4 SpotAtlasMatrix(const SpotShadow& shadow, std::uint32_t tile, SpotAtlasGrid grid);

    /**
     * @brief One record of the frame's spot shadow buffer, as `Fenrir/Frame.glsl` declares `FenrirSpotShadow` (std430):
     * how a shadowed spot light's tile is read. Record i belongs to the light buffer's record i.
     */
    struct GpuSpotShadow
    {
        /** @brief SpotAtlasMatrix: world to the atlas, divided by w. */
        Math::Mat4 atlas{1.0f};
        /**
         * @brief x: the normal-offset bias and y: the slope-scaled bias, in texels; z: a texel's world size one unit
         * along the light (SpotShadow::texelScale); w: the filter's radius in texels of the atlas.
         */
        Math::Vec4 bias{0.0f};
        /** @brief The u and v the filter may read between: the tile, less the filter's reach and a texel. xy low, zw
         * high. */
        Math::Vec4 bounds{0.0f};
    };
    static_assert(sizeof(GpuSpotShadow) == 96);

    /** @brief The record of `light`'s `shadow` in tile `tile` of `grid`, tiles of `mapSize` texels. */
    [[nodiscard]] GpuSpotShadow ToGpuSpotShadow(const PunctualLight& light, const SpotShadow& shadow,
                                                std::uint32_t tile, SpotAtlasGrid grid, std::uint32_t mapSize);
} // namespace Fenrir::Render
