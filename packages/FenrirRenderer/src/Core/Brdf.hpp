#pragma once

#include <cstdint>

#include "FenrirMath/Math.hpp"

namespace Fenrir::Render
{
    // The CPU mirror of Fenrir/Brdf.glsl and of what Fenrir/Lighting.glsl makes of it (ADR-0048), kept step for step
    // with the shaders so the CPU tests check the maths every lit material shares. `a` is perceptual roughness squared
    // throughout.

    /** @brief Trowbridge-Reitz (GGX) normal distribution at the cosine `nDotH` between the normal and the half vector.
     */
    [[nodiscard]] float GgxDistribution(float nDotH, float a) noexcept;

    /**
     * @brief Height-correlated Smith visibility (Heitz 2014, as Filament): the masking-shadowing term G divided by
     * 4 N.V N.L, so a specular lobe is D x V x F.
     */
    [[nodiscard]] float SmithVisibility(float nDotV, float nDotL, float a) noexcept;

    /**
     * @brief The CPU mirror of builtin:brdf-lut: the split sum's scale (x) and bias (y) that f0 takes, f0 x scale +
     * bias, at `nDotV` and perceptual `roughness`, integrated over `sampleCount` Hammersley points by importance
     * sampling GGX. Their sum is the share of light a white metal's single-scattering lobe reflects (Ess).
     */
    [[nodiscard]] Math::Vec2 BrdfScaleBias(float nDotV, float roughness, std::uint32_t sampleCount = 512) noexcept;

    /**
     * @brief Direct light's multi-scatter energy compensation (Fdez-Aguera 2019, in Filament's form): the specular lobe
     * times 1 + f0 (1 / Ess - 1), giving back what the single-scattering lobe loses to its own microfacets.
     */
    [[nodiscard]] Math::Vec3 EnergyCompensation(Math::Vec3 f0, Math::Vec2 scaleBias) noexcept;

    /** @brief How much of the sky a surface reflects specularly, by the split sum with multi-scatter (Fdez-Aguera
     * 2019). */
    struct SkyReflectance
    {
        /** @brief The single-scattering share, f0 x scale + bias: what the prefiltered cube's light is multiplied by.
         */
        Math::Vec3 singleScatter;
        /** @brief The multiple-scattering share, Fms Ems: what the sky's irradiance is multiplied by. */
        Math::Vec3 multiScatter;
    };

    /** @brief The sky's specular reflectance of a surface whose reflectance at normal incidence is `f0`. */
    [[nodiscard]] SkyReflectance SkySpecularReflectance(Math::Vec3 f0, Math::Vec2 scaleBias) noexcept;

    /**
     * @brief Specular anti-aliasing (Tokuyoshi and Kaplanyan 2019, with Filament's constants): perceptual `roughness`
     * widened by the variance of the shading normal across a pixel, whose screen-space derivatives are `dNdx` and
     * `dNdy`, so a highlight thinner than a pixel spreads over it rather than flickering in and out. A normal that does
     * not change across the pixel keeps its roughness; the widening is capped.
     */
    [[nodiscard]] float AntiAliasedRoughness(float roughness, Math::Vec3 dNdx, Math::Vec3 dNdy) noexcept;
} // namespace Fenrir::Render
