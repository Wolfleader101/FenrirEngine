// The CPU mirror of builtin:output's tonemappers (ADR-0049): kept step for step with Shaders/Builtin/output.frag,
// which the golden images hold to it, so a change to one is a change to both. Matrices are written as GLSL writes them,
// column by column.
#include "Core/Tonemapping.hpp"

#include <algorithm>
#include <cmath>

namespace Fenrir::Render
{
    namespace
    {
        using Math::Mat3;
        using Math::Vec3;

        Vec3 Saturate(Vec3 colour)
        {
            return glm::clamp(colour, Vec3(0.0f), Vec3(1.0f));
        }

        // AgX as three.js r170 writes it, after Filament and Blender: into Rec. 2020, the inset matrix, a log2
        // encoding from 10 stops under to 6.5 stops over mid grey, the sigmoid's polynomial fit, the outset matrix,
        // the fit's 2.2 gamma undone, and back to Rec. 709 (sRGB's primaries).
        Vec3 AgX(Vec3 colour)
        {
            const Mat3 srgbToRec2020(Vec3(0.6274f, 0.0691f, 0.0164f), Vec3(0.3293f, 0.9195f, 0.0880f),
                                     Vec3(0.0433f, 0.0113f, 0.8956f));
            const Mat3 rec2020ToSrgb(Vec3(1.6605f, -0.1246f, -0.0182f), Vec3(-0.5876f, 1.1329f, -0.1006f),
                                     Vec3(-0.0728f, -0.0083f, 1.1187f));
            const Mat3 inset(Vec3(0.856627153315983f, 0.137318972929847f, 0.11189821299995f),
                             Vec3(0.0951212405381588f, 0.761241990602591f, 0.0767994186031903f),
                             Vec3(0.0482516061458583f, 0.101439036467562f, 0.811302368396859f));
            const Mat3 outset(Vec3(1.1271005818144368f, -0.1413297634984383f, -0.14132976349843826f),
                              Vec3(-0.11060664309660323f, 1.157823702216272f, -0.11060664309660294f),
                              Vec3(-0.016493938717834573f, -0.016493938717834257f, 1.2519364065950405f));
            // log2(0.18 x 2^-10) and log2(0.18 x 2^6.5).
            constexpr float minEv = -12.47393f;
            constexpr float maxEv = 4.026069f;

            Vec3 x = inset * (srgbToRec2020 * colour);
            x = glm::log2(glm::max(x, Vec3(1e-10f)));
            x = Saturate((x - minEv) / (maxEv - minEv));
            const Vec3 x2 = x * x;
            const Vec3 x4 = x2 * x2;
            x = 15.5f * x4 * x2 - 40.14f * x4 * x + 31.96f * x4 - 6.868f * x2 * x + 0.4298f * x2 + 0.1191f * x -
                0.00232f;
            x = outset * x;
            x = glm::pow(glm::max(x, Vec3(0.0f)), Vec3(2.2f));
            return Saturate(rec2020ToSrgb * x);
        }

        // Khronos PBR Neutral (https://github.com/KhronosGroup/ToneMapping): the darkest channel's small offset taken
        // off, and above 0.76 the peak compressed towards 1 and the colour desaturated towards it.
        Vec3 PbrNeutral(Vec3 colour)
        {
            constexpr float startCompression = 0.8f - 0.04f;
            constexpr float desaturation = 0.15f;
            const float x = std::min({colour.r, colour.g, colour.b});
            const float offset = x < 0.08f ? x - 6.25f * x * x : 0.04f;
            colour -= offset;
            const float peak = std::max({colour.r, colour.g, colour.b});
            if (peak < startCompression)
            {
                return colour;
            }
            constexpr float d = 1.0f - startCompression;
            const float newPeak = 1.0f - d * d / (peak + d - startCompression);
            colour *= newPeak / peak;
            const float g = 1.0f - 1.0f / (desaturation * (peak - newPeak) + 1.0f);
            return glm::mix(colour, Vec3(newPeak), g);
        }

        // Stephen Hill's fit (BakingLab's ACES.hlsl): sRGB into ACES's AP1 with the RRT's saturation, a rational fit
        // of the RRT and ODT curves, and back to sRGB.
        Vec3 Aces(Vec3 colour)
        {
            const Mat3 input(Vec3(0.59719f, 0.07600f, 0.02840f), Vec3(0.35458f, 0.90834f, 0.13383f),
                             Vec3(0.04823f, 0.01566f, 0.83777f));
            const Mat3 output(Vec3(1.60475f, -0.10208f, -0.00327f), Vec3(-0.53108f, 1.10813f, -0.07276f),
                              Vec3(-0.07367f, -0.00605f, 1.07602f));
            const Vec3 v = input * colour;
            const Vec3 a = v * (v + 0.0245786f) - 0.000090537f;
            const Vec3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
            return Saturate(output * (a / b));
        }
    } // namespace

    float ExposureScale(float exposure) noexcept
    {
        return std::exp2(exposure);
    }

    Math::Vec3 Tonemap(Tonemapper tonemapper, Math::Vec3 linear) noexcept
    {
        switch (tonemapper)
        {
        case Tonemapper::AgX:
            return AgX(linear);
        case Tonemapper::PbrNeutral:
            return PbrNeutral(linear);
        case Tonemapper::Aces:
            return Aces(linear);
        case Tonemapper::None:
            break;
        }
        return Saturate(linear);
    }
} // namespace Fenrir::Render
