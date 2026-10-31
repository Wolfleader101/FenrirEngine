#include "Core/Brdf.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Fenrir::Render
{
    namespace
    {
        constexpr float Pi = std::numbers::pi_v<float>;

        // Filament's: the variance of the pixel's normal filter, and the cap on the kernel it widens a^2 by.
        constexpr float AntiAliasingVariance = 0.15f;
        constexpr float AntiAliasingThreshold = 0.2f;

        /** @brief The radical inverse of `i` in base 2: the second coordinate of a Hammersley point. */
        float RadicalInverse(std::uint32_t i) noexcept
        {
            i = (i << 16u) | (i >> 16u);
            i = ((i & 0x55555555u) << 1u) | ((i & 0xAAAAAAAAu) >> 1u);
            i = ((i & 0x33333333u) << 2u) | ((i & 0xCCCCCCCCu) >> 2u);
            i = ((i & 0x0F0F0F0Fu) << 4u) | ((i & 0xF0F0F0F0u) >> 4u);
            i = ((i & 0x00FF00FFu) << 8u) | ((i & 0xFF00FF00u) >> 8u);
            return static_cast<float>(i) * 2.3283064365386963e-10f;
        }
    } // namespace

    float GgxDistribution(float nDotH, float a) noexcept
    {
        const float a2 = a * a;
        const float d = nDotH * nDotH * (a2 - 1.0f) + 1.0f;
        return a2 / (Pi * d * d);
    }

    float SmithVisibility(float nDotV, float nDotL, float a) noexcept
    {
        const float a2 = a * a;
        const float lambdaV = nDotL * std::sqrt((nDotV - nDotV * a2) * nDotV + a2);
        const float lambdaL = nDotV * std::sqrt((nDotL - nDotL * a2) * nDotL + a2);
        return 0.5f / (lambdaV + lambdaL);
    }

    Math::Vec2 BrdfScaleBias(float nDotV, float roughness, std::uint32_t sampleCount) noexcept
    {
        nDotV = std::max(nDotV, 1e-4f);
        const float a = roughness * roughness;
        const Math::Vec3 v(std::sqrt(1.0f - nDotV * nDotV), 0.0f, nDotV);
        Math::Vec2 sum(0.0f);
        for (std::uint32_t i = 0; i < sampleCount; ++i)
        {
            const float phi = 2.0f * Pi * static_cast<float>(i) / static_cast<float>(sampleCount);
            const float xi = RadicalInverse(i);
            const float cosTheta = std::sqrt((1.0f - xi) / (1.0f + (a * a - 1.0f) * xi));
            const float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
            const Math::Vec3 h(sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta);
            const Math::Vec3 l = 2.0f * glm::dot(v, h) * h - v;
            const float vDotH = std::max(glm::dot(v, h), 0.0f);
            if (l.z > 0.0f)
            {
                // The sample's weight over the GGX pdf D(h) (h.n) / (4 v.h).
                const float g = SmithVisibility(nDotV, l.z, a) * 4.0f * l.z * vDotH / std::max(h.z, 1e-4f);
                const float fresnel = std::pow(1.0f - vDotH, 5.0f);
                sum += Math::Vec2((1.0f - fresnel) * g, fresnel * g);
            }
        }
        return sum / static_cast<float>(sampleCount);
    }

    Math::Vec3 EnergyCompensation(Math::Vec3 f0, Math::Vec2 scaleBias) noexcept
    {
        const float singleScatter = std::max(scaleBias.x + scaleBias.y, 1e-4f);
        return Math::Vec3(1.0f) + f0 * (1.0f / singleScatter - 1.0f);
    }

    SkyReflectance SkySpecularReflectance(Math::Vec3 f0, Math::Vec2 scaleBias) noexcept
    {
        const Math::Vec3 singleScatter = f0 * scaleBias.x + scaleBias.y;
        // What a white metal's single-scattering lobe does not reflect leaves after further bounces, each reflecting
        // the hemisphere's average Fresnel: a geometric series.
        const float lost = 1.0f - (scaleBias.x + scaleBias.y);
        const Math::Vec3 averageFresnel = f0 + (Math::Vec3(1.0f) - f0) / 21.0f;
        const Math::Vec3 multiScatter = singleScatter * averageFresnel / (Math::Vec3(1.0f) - lost * averageFresnel);
        return {singleScatter, multiScatter * lost};
    }

    float AntiAliasedRoughness(float roughness, Math::Vec3 dNdx, Math::Vec3 dNdy) noexcept
    {
        const float variance = AntiAliasingVariance * (glm::dot(dNdx, dNdx) + glm::dot(dNdy, dNdy));
        const float kernel = std::min(2.0f * variance, AntiAliasingThreshold);
        const float a = roughness * roughness;
        return std::sqrt(std::sqrt(std::clamp(a * a + kernel, 0.0f, 1.0f)));
    }
} // namespace Fenrir::Render
