#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "Core/LightShadows.hpp"
#include "Core/PunctualLights.hpp"
#include "Core/SpotShadows.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir::Render
{
    // Point shadows (ADR-0050): a budget of shadowed point lights, the nearest to the views each frame, each a cube of
    // one cube array pooled by the budget, holding the distance from the light over its range; drawn without a
    // geometry shader, read by `Fenrir/Shadows.glsl`.

    /** @brief The faces of a cube, in the order a cube texture's layers hold them: +X, -X, +Y, -Y, +Z, -Z. */
    inline constexpr std::uint32_t CubeFaceCount = 6;

    /**
     * @brief The point lights of `lights` with castShadows that keep their shadows, as ChooseSpotShadows chooses spot
     * lights: the `budget` nearest to any of the cameras at `cameras`, the nearer first, ties in list order.
     */
    [[nodiscard]] ShadowChoice ChoosePointShadows(std::span<const PunctualLight> lights,
                                                  std::span<const Math::Vec3> cameras, std::uint32_t budget);

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
} // namespace Fenrir::Render
