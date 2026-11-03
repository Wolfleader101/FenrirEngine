#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "Core/LightShadows.hpp"
#include "Core/PunctualLights.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir::Render
{
    // Point shadows (ADR-0050): a budget of shadowed point lights, the nearest to the views each frame, each a cube of
    // one cube array pooled by the budget, holding the distance from the light over its range; drawn without a
    // geometry shader, read by `Fenrir/Shadows.glsl`.

    /** @brief RendererConfig's default point shadow budget and cube face side. */
    inline constexpr ShadowBudgetConfig DefaultPointShadows{4, 512};

    /** @brief The faces of a cube, in the order a cube texture's layers hold them: +X, -X, +Y, -Y, +Z, -Z. */
    inline constexpr std::uint32_t CubeFaceCount = 6;

    /**
     * @brief World directions to cube face `face`'s view space, as `Fenrir/CubeFaces.glsl`'s FenrirFaceTurn: looking
     * down its -Z along the face's axis, its +X and +Y towards the face's NDC +x and +y (its first row at the top), so
     * a 90-degree perspective draws what a lookup of the cube along those directions reads. It mirrors, as cube faces
     * do.
     */
    [[nodiscard]] Math::Mat3 CubeFaceTurn(std::uint32_t face) noexcept;

    /** @brief A point light's shadow: what its cube's faces are drawn with. */
    struct PointShadow
    {
        /** @brief World to the light's own frame: the light's position to the origin, not turned. */
        Math::Mat4 view{1.0f};
        /** @brief World to each face's view (CubeFaceTurn after `view`). */
        std::array<Math::Mat4, CubeFaceCount> faceViews{};
        /** @brief A face's view to its clip space: a 90-degree square infinite reversed-Z perspective, near `near`. */
        Math::Mat4 projection{1.0f};
        /** @brief The world size of one of its texels one unit along a face's axis: it grows with the distance. */
        float texelScale = 0.0f;
        /** @brief Its near plane's distance from the light. */
        float near = 0.0f;
    };

    /**
     * @brief The shadow of the point `light`, its faces `mapSize` texels square: six 90-degree perspectives from its
     * position, their near plane a two-hundredth of its range, held between 0.001 and 0.1. What they store is the
     * distance from the light over its range, so its range is the far plane.
     */
    [[nodiscard]] PointShadow FitPointShadow(const PunctualLight& light, std::uint32_t mapSize);

    /**
     * @brief One record of the frame's point shadow buffer, as `Fenrir/Frame.glsl` declares `FenrirPointShadow`
     * (std430): how a shadowed point light's cube is read. Record i belongs to cube i.
     */
    struct GpuPointShadow
    {
        /**
         * @brief x: the normal-offset bias and y: the slope-scaled bias, in texels; z: a texel's world size one unit
         * along a face's axis (PointShadow::texelScale); w: the filter's radius in texels.
         */
        Math::Vec4 bias{0.0f};
    };
    static_assert(sizeof(GpuPointShadow) == 16);

    /** @brief The record of `light`'s `shadow`. */
    [[nodiscard]] GpuPointShadow ToGpuPointShadow(const PunctualLight& light, const PointShadow& shadow);

    /** @brief The point lights' kind of ShadowPool: each shadowed light a cube of one cube array, a cube per budget. */
    struct PointShadowKind
    {
        /** @brief What the pool draws each light's shadow with: its six faces' views. */
        using Shadow = PointShadow;
        /** @brief One light's record in the ring set 0 binds. */
        using Record = GpuPointShadow;
        /** @brief The kind of light the pool chooses from. */
        static constexpr LightKind Light = LightKind::Point;
        /** @brief The kind's name in the budget's warning. */
        static constexpr const char* Name = "point";
        /** @brief The least side of a light's map, in texels, that the config's map size is held to. */
        static constexpr std::uint32_t MinMapSize = 1;
        /** @brief The maps' label on the device. */
        static constexpr const char* MapLabel = "Point shadow cubes";
        /** @brief The maps' name in the frame's graph. */
        static constexpr const char* MapResource = "PointShadowCubes";
        /** @brief The record ring's label. */
        static constexpr const char* RecordsLabel = "Point shadows";

        /** @brief FitPointShadow, its faces the config's map size square. */
        [[nodiscard]] static PointShadow Fit(const PunctualLight& light, const ShadowBudgetConfig& config);
        /** @brief ToGpuPointShadow; record i belongs to cube i. */
        [[nodiscard]] static GpuPointShadow ToRecord(const PunctualLight& light, const PointShadow& shadow,
                                                     std::uint32_t cube, const ShadowBudgetConfig& config);
        /** @brief A Depth32Float CubeArray of a cube per budget, its faces the map size square. */
        [[nodiscard]] static Graphics::TextureDesc MapDesc(const ShadowBudgetConfig& config);
        /** @brief One cube of depth 0, which lights everything, bound where no point light casts. */
        [[nodiscard]] static Graphics::TextureHandle MakeFallback(Graphics::GraphicsDevice& device);
    };
} // namespace Fenrir::Render
