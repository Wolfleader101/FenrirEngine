#pragma once

#include <cstdint>
#include <span>

#include "Core/PunctualLights.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir::Render
{
    // Rect lights (ADR-0048): what the frame's light buffer holds of one, and the CPU mirror of how
    // Fenrir/Lighting.glsl shades it with linearly transformed cosines (Heitz, Dupuy, Hill and Neubelt 2016), kept step
    // for step with the shader so the CPU tests check its maths. The rect's light is a cosine lobe's integral over it:
    // a plain cosine for the diffuse, and for the specular a cosine turned into the shape of GGX by a 3 x 3 transform
    // from the first lookup table. The integral sums each edge's vector form factor and clips the horizon with Hill's
    // sphere approximation from the second table (the "clipless" form), so no polygon is clipped.

    /** @brief One rect light read from the scene, in world space. */
    struct RectLight
    {
        /** @brief Its centre. */
        Math::Vec3 position{0.0f};
        /** @brief Along its width (its entity's world +X), unit length. */
        Math::Vec3 right{1.0f, 0.0f, 0.0f};
        /** @brief Along its height (its entity's world +Y), unit length. It faces right x up's opposite, world -Z. */
        Math::Vec3 up{0.0f, 1.0f, 0.0f};
        /** @brief Its colour times its intensity, linear: its radiance. */
        Math::Vec3 colour{0.0f};
        /** @brief Its size in world units. */
        float width = 0.0f;
        float height = 0.0f;
        /** @brief Whether it shines from its back face as well. */
        bool twoSided = false;
        /** @brief The distance from it at which its light has fallen to zero, in world units. */
        float range = 0.0f;
        /** @brief Whether the scene draws its surface, so its specular yields to reflections of it (ADR-0056). */
        bool visibleSurface = true;
    };

    /** @brief `light` as its light buffer record: kind Rect, facing -(right x up). */
    [[nodiscard]] GpuLight ToGpuLight(const RectLight& light) noexcept;

    /** @brief The side of both lookup tables, in texels. */
    inline constexpr std::uint32_t LtcSide = 64;

    /**
     * @brief The first table's RGBA16F texels, row by row: the inverse transform's m00, m02, m20 and m22 (column, row)
     * by perceptual roughness across and sqrt(1 - N.V) down. Set 0 binding 5, `fenrirLtcMatrix`.
     */
    [[nodiscard]] std::span<const std::uint16_t> LtcMatrixTexels() noexcept;

    /**
     * @brief The second table's: alpha is the form factor of a sphere clipped by the horizon, by the cosine of its
     * centre's elevation (mapped to [0, 1]) across and its unclipped form factor down. Set 0 binding 6,
     * `fenrirLtcAmplitude`.
     */
    [[nodiscard]] std::span<const std::uint16_t> LtcAmplitudeTexels() noexcept;

    /**
     * @brief `texels` sampled at `uv` as the GPU samples them: bilinear, clamped at the edges. `uv` is in [0, 1]
     * across the whole table; LtcUv maps a lookup into it.
     */
    [[nodiscard]] Math::Vec4 SampleLtcTable(std::span<const std::uint16_t> texels, Math::Vec2 uv) noexcept;

    /** @brief A lookup's [0, 1] coordinates moved onto the table's first and last texels' centres. */
    [[nodiscard]] Math::Vec2 LtcUv(Math::Vec2 lookup) noexcept;

    /**
     * @brief The vector form factor of the arc from the unit vector `from` to `to`: their cross product times
     * theta / (2 pi sin theta), from Hill's rational fit, accurate to the end of the range where the two are opposite.
     */
    [[nodiscard]] Math::Vec3 LtcEdge(Math::Vec3 from, Math::Vec3 to) noexcept;

    /**
     * @brief The integral over the rect `light` of a cosine lobe about +Z in the frame `toLobe` takes world directions
     * into, seen from `point`: its form factor, 0 to 1, clipped to the lobe's upper hemisphere. 0 from behind a
     * one-sided light; from behind a two-sided one, as from in front.
     */
    [[nodiscard]] float LtcIntegral(Math::Vec3 point, const Math::Mat3& toLobe, const GpuLight& light) noexcept;

    /**
     * @brief How the rect `light`'s light is windowed by distance: (1 - (d / range)^4)^2, d the distance from `point`
     * to the rect's nearest point; 0 at and past the range. The lobes' integrals already fall with distance.
     */
    [[nodiscard]] float RectWindow(Math::Vec3 point, const GpuLight& light) noexcept;

    /**
     * @brief The frame of a surface at `normal` seen along `view` (both unit, towards the viewer): world directions
     * into tangent space, x towards the view, z the normal.
     */
    [[nodiscard]] Math::Mat3 LtcSurfaceFrame(Math::Vec3 normal, Math::Vec3 view) noexcept;

    /**
     * @brief The diffuse light, unwindowed, a white Lambertian surface at `point` facing `normal` takes from the rect
     * `light` of radiance 1: the form factor of the rect clipped to the surface's hemisphere, 0 to 1.
     */
    [[nodiscard]] float RectDiffuse(Math::Vec3 point, Math::Vec3 normal, const GpuLight& light) noexcept;
} // namespace Fenrir::Render
