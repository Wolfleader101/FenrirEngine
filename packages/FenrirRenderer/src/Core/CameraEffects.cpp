// The CPU mirror of builtin:output's chromatic aberration, vignette and film grain (ADR-0059): kept step for step with
// Shaders/Builtin/output.frag, which the GL tests hold to it, so a change to one is a change to both.
#include "Core/CameraEffects.hpp"

#include <algorithm>
#include <cmath>

#include "Core/ColourGrading.hpp"

namespace Fenrir::Render
{
    namespace
    {
        using Math::Vec2;
        using Math::Vec3;

        float Smoothstep(float edge0, float edge1, float x)
        {
            const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }
    } // namespace

    AberrationReads AberrationReadPoints(Vec2 uv, float intensity) noexcept
    {
        const Vec2 fromCentre = uv - Vec2(0.5f);
        return {Vec2(0.5f) + fromCentre * (1.0f - intensity), uv, Vec2(0.5f) + fromCentre * (1.0f + intensity)};
    }

    float VignetteFalloff(const VignetteSettings& settings, Vec2 uv, float aspect) noexcept
    {
        if (settings.intensity <= 0.0f)
        {
            return 1.0f;
        }
        // Each axis in halves of the view's height when round, in halves of its own length when not.
        const Vec2 scale(1.0f + (aspect - 1.0f) * settings.roundness, 1.0f);
        const float distance = glm::length((uv - Vec2(0.5f)) * scale) / glm::length(Vec2(0.5f) * scale);
        const float edge = settings.radius + std::max(settings.smoothness, 1e-4f);
        return 1.0f - settings.intensity * Smoothstep(settings.radius, edge, distance);
    }

    float GrainNoise(std::uint32_t x, std::uint32_t y, std::uint32_t frame) noexcept
    {
        std::uint32_t a = x * 1664525u + 1013904223u;
        std::uint32_t b = y * 1664525u + 1013904223u;
        std::uint32_t c = frame * 1664525u + 1013904223u;
        a += b * c;
        b += c * a;
        c += a * b;
        a ^= a >> 16u;
        b ^= b >> 16u;
        c ^= c >> 16u;
        a += b * c;
        b += c * a;
        // Two 24-bit uniforms, exact in a float, summed and divided by 2^24: a triangle on [0, 2), less 1.
        return static_cast<float>((a >> 8u) + (b >> 8u)) * (1.0f / 16777216.0f) - 1.0f;
    }

    Vec3 ApplyFilmGrain(const FilmGrainSettings& settings, Vec3 display, std::uint32_t x, std::uint32_t y,
                        std::uint32_t frame) noexcept
    {
        if (settings.intensity <= 0.0f)
        {
            return display;
        }
        const float size = std::max(settings.size, 1.0f);
        const auto cellX = static_cast<std::uint32_t>(std::floor(static_cast<float>(x) / size));
        const auto cellY = static_cast<std::uint32_t>(std::floor(static_cast<float>(y) / size));
        const float noise = GrainNoise(cellX, cellY, frame);
        const Vec3 encoded = glm::pow(glm::max(display, Vec3(0.0f)), Vec3(1.0f / 2.2f));
        const float luminance = std::clamp(Luminance(encoded), 0.0f, 1.0f);
        const float weight = 1.0f + (4.0f * luminance * (1.0f - luminance) - 1.0f) * settings.response;
        const Vec3 grained = glm::clamp(encoded + Vec3(settings.intensity * weight * noise), Vec3(0.0f), Vec3(1.0f));
        return glm::pow(grained, Vec3(2.2f));
    }
} // namespace Fenrir::Render
