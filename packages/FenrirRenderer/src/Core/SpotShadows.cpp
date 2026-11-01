#include "Core/SpotShadows.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Core/Cascades.hpp"
#include "Core/SceneFrame.hpp"

namespace Fenrir::Render
{
    SpotAtlasGrid SpotAtlasGridOf(std::uint32_t budget) noexcept
    {
        if (budget == 0)
        {
            return {};
        }
        auto columns = static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<double>(budget))));
        // Exact for squares, whatever sqrt rounds them to.
        while ((columns - 1) * (columns - 1) >= budget)
        {
            --columns;
        }
        return {columns, (budget + columns - 1) / columns};
    }

    SpotShadowChoice ChooseSpotShadows(std::span<const PunctualLight> lights, std::span<const Math::Vec3> cameras,
                                       std::uint32_t budget)
    {
        SpotShadowChoice choice;
        std::vector<float> distance(lights.size(), 0.0f);
        for (std::size_t i = 0; i < lights.size(); ++i)
        {
            const PunctualLight& light = lights[i];
            if (light.kind != LightKind::Spot || !light.castShadows)
            {
                continue;
            }
            choice.lights.push_back(i);
            float nearest = cameras.empty() ? 0.0f : std::numeric_limits<float>::infinity();
            for (const Math::Vec3 camera : cameras)
            {
                nearest = std::min(nearest, glm::distance(light.position, camera));
            }
            distance[i] = nearest;
        }
        choice.wanted = choice.lights.size();
        std::ranges::stable_sort(choice.lights,
                                 [&](std::size_t a, std::size_t b) { return distance[a] < distance[b]; });
        choice.lights.resize(std::min<std::size_t>(choice.lights.size(), budget));
        return choice;
    }

    SpotShadow FitSpotShadow(const PunctualLight& light, std::uint32_t mapSize)
    {
        SpotShadow shadow;
        const auto size = static_cast<float>(std::max(mapSize, 16u));
        // The cone's edge with the filter's reach and a texel more inside each side of the tile, so a lookup at the
        // cone's edge never filters past it.
        const float spare = ShadowFilterRadius + 1.0f;
        const float halfAngle = std::clamp(light.outerAngle, 1e-3f, MaxSpotShadowAngle);
        const float halfTan = std::tan(halfAngle) * size / (size - 2.0f * spare);
        shadow.texelScale = 2.0f * halfTan / size;
        shadow.near = std::clamp(light.range / 200.0f, 1e-3f, 0.1f);
        Math::Mat4 toLight(1.0f);
        toLight[3] = Math::Vec4(-light.position, 1.0f);
        shadow.view = LightRotation(light.direction) * toLight;
        shadow.projection = ReversedZPerspective(2.0f * std::atan(halfTan), 1.0f, shadow.near);
        shadow.viewProjection = shadow.projection * shadow.view;
        return shadow;
    }

    Math::Mat4 SpotAtlasMatrix(const SpotShadow& shadow, std::uint32_t tile, SpotAtlasGrid grid)
    {
        // Clip x in [-w, w] across its tile, and clip +y up to v 0 at the tile's top (ADR-0016); z and w as drawn. The
        // tile's offset multiplies w, so it survives the shader's divide by it.
        const std::uint32_t across = std::max(grid.columns, 1u);
        const auto columns = static_cast<float>(across);
        const auto rows = static_cast<float>(std::max(grid.rows, 1u));
        Math::Mat4 atlas(1.0f);
        atlas[0][0] = 0.5f / columns;
        atlas[1][1] = -0.5f / rows;
        atlas[3][0] = (0.5f + static_cast<float>(tile % across)) / columns;
        atlas[3][1] = (0.5f + static_cast<float>(tile / across)) / rows;
        return atlas * shadow.viewProjection;
    }

    GpuSpotShadow ToGpuSpotShadow(const PunctualLight& light, const SpotShadow& shadow, std::uint32_t tile,
                                  SpotAtlasGrid grid, std::uint32_t mapSize)
    {
        const std::uint32_t across = std::max(grid.columns, 1u);
        const auto columns = static_cast<float>(across);
        const auto rows = static_cast<float>(std::max(grid.rows, 1u));
        const auto size = static_cast<float>(std::max(mapSize, 1u));
        const auto column = static_cast<float>(tile % across);
        const auto row = static_cast<float>(tile / across);
        const Math::Vec2 texel(1.0f / (columns * size), 1.0f / (rows * size));
        const Math::Vec2 reach = (ShadowFilterRadius + 1.0f) * texel;
        const Math::Vec2 low = Math::Vec2(column / columns, row / rows) + reach;
        const Math::Vec2 high = Math::Vec2((column + 1.0f) / columns, (row + 1.0f) / rows) - reach;

        GpuSpotShadow gpu;
        gpu.atlas = SpotAtlasMatrix(shadow, tile, grid);
        gpu.bias = Math::Vec4(std::max(light.shadowNormalBias, 0.0f), std::max(light.shadowSlopeBias, 0.0f),
                              shadow.texelScale, ShadowFilterRadius);
        gpu.bounds = Math::Vec4(low, high);
        return gpu;
    }
} // namespace Fenrir::Render
