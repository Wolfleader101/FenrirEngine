// The Output pass's tonemappers and exposure on the CPU (the spec's CPU unit tests; ADR-0049): the C++ mirror of
// builtin:output's curves at black, mid grey, white, a bright HDR grey and a bright orange, against reference values
// worked from each curve's published formula in double precision (AgX: three.js r170 after Filament and Blender;
// PbrNeutral: Khronos PBR Neutral; Aces: Stephen Hill's fit of the ACES RRT and sRGB ODT).

#include <array>
#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "Core/Tonemapping.hpp"
#include "FenrirMath/Math.hpp"

using Fenrir::Math::Vec3;
using Fenrir::Render::Tonemap;
using Fenrir::Render::Tonemapper;

namespace
{
    /** @brief One input colour, linear, and what a tonemapper maps it to, linear in [0, 1]. */
    struct Reference
    {
        Vec3 input;
        Vec3 expected;
    };

    // Black, mid grey, white, a grey four stops over white, and a bright orange.
    const Vec3 Black(0.0f);
    const Vec3 MidGrey(0.18f);
    const Vec3 White(1.0f);
    const Vec3 Bright(16.0f);
    const Vec3 Orange(4.0f, 1.0f, 0.25f);

    bool Near(Vec3 a, Vec3 b, float tolerance = 1e-4f)
    {
        return glm::all(glm::lessThan(glm::abs(a - b), Vec3(tolerance)));
    }

    void RequireCurve(Tonemapper tonemapper, const std::array<Reference, 5>& references)
    {
        for (const Reference& reference : references)
        {
            const Vec3 actual = Tonemap(tonemapper, reference.input);
            INFO("(" << reference.input.x << ", " << reference.input.y << ", " << reference.input.z << ") maps to ("
                     << actual.x << ", " << actual.y << ", " << actual.z << "), not the reference ("
                     << reference.expected.x << ", " << reference.expected.y << ", " << reference.expected.z << ")");
            REQUIRE(Near(actual, reference.expected));
        }
    }
} // namespace

TEST_CASE("None clamps each channel to [0, 1], the old Output", "[renderer][tonemapping]")
{
    RequireCurve(Tonemapper::None, {{
                                       {Black, Black},
                                       {MidGrey, MidGrey},
                                       {White, White},
                                       {Bright, White},
                                       {Orange, Vec3(1.0f, 1.0f, 0.25f)},
                                   }});
}

TEST_CASE("AgX matches its reference values", "[renderer][tonemapping]")
{
    RequireCurve(Tonemapper::AgX, {{
                                      {Black, Black},
                                      {MidGrey, Vec3(0.214549f, 0.214502f, 0.214499f)},
                                      {White, Vec3(0.590229f, 0.590136f, 0.590102f)},
                                      {Bright, Vec3(0.995212f, 0.995098f, 0.995009f)},
                                      {Orange, Vec3(0.936480f, 0.623567f, 0.437369f)},
                                  }});
}

TEST_CASE("PbrNeutral follows Khronos PBR Neutral", "[renderer][tonemapping]")
{
    RequireCurve(Tonemapper::PbrNeutral, {{
                                             {Black, Black},
                                             // Below the start of compression only the 0.04 offset is taken off.
                                             {MidGrey, Vec3(0.14f)},
                                             {White, Vec3(0.869091f)},
                                             {Bright, Vec3(0.996269f)},
                                             {Orange, Vec3(0.983256f, 0.468299f, 0.339560f)},
                                         }});
}

TEST_CASE("Aces follows Stephen Hill's fit", "[renderer][tonemapping]")
{
    RequireCurve(Tonemapper::Aces, {{
                                       {Black, Black},
                                       {MidGrey, Vec3(0.105591f, 0.105591f, 0.105590f)},
                                       {White, Vec3(0.619115f, 0.619115f, 0.619109f)},
                                       {Bright, Vec3(0.989935f, 0.989935f, 0.989925f)},
                                       {Orange, Vec3(0.994891f, 0.663381f, 0.316685f)},
                                   }});
}

TEST_CASE("Exposure scales the colour by 2 to the power of its stops", "[renderer][tonemapping]")
{
    INFO("0 EV leaves the colour as it is");
    REQUIRE(Fenrir::Render::ExposureScale(0.0f) == 1.0f);
    INFO("each stop up doubles it");
    REQUIRE(std::abs(Fenrir::Render::ExposureScale(1.0f) - 2.0f) < 1e-6f);
    REQUIRE(std::abs(Fenrir::Render::ExposureScale(3.0f) - 8.0f) < 1e-5f);
    INFO("each stop down halves it");
    REQUIRE(std::abs(Fenrir::Render::ExposureScale(-2.0f) - 0.25f) < 1e-6f);
    INFO("a fraction of a stop scales by its root of two");
    REQUIRE(std::abs(Fenrir::Render::ExposureScale(0.5f) - std::sqrt(2.0f)) < 1e-6f);
}
