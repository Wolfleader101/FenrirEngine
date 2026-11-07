#pragma once

#include <cstdint>

#include "FenrirMath/Math.hpp"

namespace Fenrir::Render
{
    /** @brief A view's chromatic aberration (ADR-0059), the core's copy of Camera::chromaticAberration. */
    struct ChromaticAberrationSettings
    {
        /**
         * @brief How far red and blue are moved apart at the view's corners, as a fraction of its half-diagonal; 0 or
         * less reads SceneColor once, through Output's plain variant.
         */
        float intensity = 0.0f;
    };

    /** @brief A view's vignette (ADR-0059), the core's copy of Camera::vignette. */
    struct VignetteSettings
    {
        /** @brief How much of the light is taken at the corners, in [0, 1]; 0 or less takes none. */
        float intensity = 0.0f;
        /** @brief Where the falloff starts, as a distance from the centre, 1 at the corners. */
        float radius = 0.5f;
        /** @brief How far past the radius the falloff takes to reach its full intensity, in the same distance. */
        float smoothness = 0.5f;
        /** @brief 1 a circle whatever the view's aspect, 0 an ellipse in the view's own proportions. */
        float roundness = 1.0f;
    };

    /** @brief A view's film grain (ADR-0059), the core's copy of Camera::filmGrain. */
    struct FilmGrainSettings
    {
        /** @brief How far the grain moves the encoded colour, at most; 0 or less adds none. */
        float intensity = 0.0f;
        /** @brief How much the grain is kept to the mid-tones, in [0, 1]: 0 the same everywhere. */
        float response = 0.8f;
        /** @brief The side of a grain's cell, in pixels: at least 1. */
        float size = 1.0f;
    };

    /** @brief Where each channel of a pixel is read from SceneColor under chromatic aberration, as UVs. */
    struct AberrationReads
    {
        Math::Vec2 red{0.0f};
        Math::Vec2 green{0.0f};
        Math::Vec2 blue{0.0f};
    };

    /**
     * @brief The CPU mirror of builtin:output's chromatic aberration: where the pixel at `uv` reads each channel.
     * Green is read where it is; red `intensity` of the way from it towards the view's centre and blue as far the other
     * way, so red shows further out and blue further in, by `intensity` of the half-diagonal at the corners.
     */
    [[nodiscard]] AberrationReads AberrationReadPoints(Math::Vec2 uv, float intensity) noexcept;

    /**
     * @brief The CPU mirror of builtin:output's vignette: what the exposed colour at `uv` (0 to 1 across the view,
     * from its top-left) is multiplied by, in a view `aspect` times as wide as it is tall. 1 at the centre and with an
     * intensity of 0 or less; 1 - intensity x smoothstep(radius, radius + smoothness, d) elsewhere, where d is the
     * distance from the centre, 1 at the corners, measured in pixels for a roundness of 1 and in each axis' own
     * half-length for 0.
     */
    [[nodiscard]] float VignetteFalloff(const VignetteSettings& settings, Math::Vec2 uv, float aspect) noexcept;

    /**
     * @brief The grain's noise for the cell (`x`, `y`) in `frame`: two uniform hashes of the three (a PCG3D hash,
     * Jarzynski and Olano 2020) summed into a triangle distribution on [-1, 1). Integer and exact, so the shader
     * matches it bit for bit.
     */
    [[nodiscard]] float GrainNoise(std::uint32_t x, std::uint32_t y, std::uint32_t frame) noexcept;

    /**
     * @brief The CPU mirror of builtin:output's film grain: `display` (tonemapped, linear, in [0, 1]) with the grain
     * of pixel (`x`, `y`) in `frame` added in a 2.2 gamma encoding, weighted by mix(1, 4 L (1 - L), response) of its
     * encoded luminance L, then decoded. The same for every channel; unchanged with an intensity of 0 or less.
     */
    [[nodiscard]] Math::Vec3 ApplyFilmGrain(const FilmGrainSettings& settings, Math::Vec3 display, std::uint32_t x,
                                            std::uint32_t y, std::uint32_t frame) noexcept;
} // namespace Fenrir::Render
