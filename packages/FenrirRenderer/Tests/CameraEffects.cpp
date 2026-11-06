// The Output pass's chromatic aberration, vignette and film grain on the CPU (ADR-0059, lighting ticket 25): the C++
// mirrors of builtin:output's read points, falloff and grain, each against values worked by hand from the ticket's
// definitions.

#include <cmath>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "Core/CameraEffects.hpp"
#include "FenrirMath/Math.hpp"

using Fenrir::Math::Vec2;
using Fenrir::Math::Vec3;
using Fenrir::Render::FilmGrainSettings;
using Fenrir::Render::VignetteSettings;

namespace
{
    bool Near(float a, float b, float tolerance = 1e-5f)
    {
        return std::abs(a - b) <= tolerance;
    }

    bool Near(Vec2 a, Vec2 b, float tolerance = 1e-5f)
    {
        return Near(a.x, b.x, tolerance) && Near(a.y, b.y, tolerance);
    }
} // namespace

TEST_CASE("Chromatic aberration leaves the view's centre where it is", "[renderer][camera-effects]")
{
    const Fenrir::Render::AberrationReads reads = Fenrir::Render::AberrationReadPoints(Vec2(0.5f), 0.2f);
    INFO("the centre's channels are read at (" << reads.red.x << ", " << reads.red.y << "), (" << reads.green.x << ", "
                                               << reads.green.y << ") and (" << reads.blue.x << ", " << reads.blue.y
                                               << ")");
    REQUIRE(Near(reads.red, Vec2(0.5f)));
    REQUIRE(Near(reads.green, Vec2(0.5f)));
    REQUIRE(Near(reads.blue, Vec2(0.5f)));
}

TEST_CASE("Chromatic aberration moves red outwards and blue inwards by its share of the half-diagonal",
          "[renderer][camera-effects]")
{
    // At the bottom-right corner, half a view from the centre each way: red is read 0.05 of that nearer the centre, so
    // what was there shows further out, and blue 0.05 further out, so what was there shows nearer in.
    const Fenrir::Render::AberrationReads reads = Fenrir::Render::AberrationReadPoints(Vec2(1.0f), 0.05f);
    INFO("the corner's red is read at (" << reads.red.x << ", " << reads.red.y << "), its blue at (" << reads.blue.x
                                         << ", " << reads.blue.y << ")");
    REQUIRE(Near(reads.red, Vec2(0.975f)));
    REQUIRE(Near(reads.green, Vec2(1.0f)));
    REQUIRE(Near(reads.blue, Vec2(1.025f)));
}

TEST_CASE("The vignette leaves the view's centre at full brightness", "[renderer][camera-effects]")
{
    VignetteSettings settings;
    settings.intensity = 1.0f;
    settings.radius = 0.0f;
    settings.smoothness = 1.0f;
    for (const float roundness : {0.0f, 1.0f})
    {
        settings.roundness = roundness;
        const float falloff = Fenrir::Render::VignetteFalloff(settings, Vec2(0.5f), 16.0f / 9.0f);
        INFO("roundness " << roundness << ": the centre is multiplied by " << falloff);
        REQUIRE(falloff == 1.0f);
    }
}

TEST_CASE("The vignette at the view's corners matches its closed form at either roundness",
          "[renderer][camera-effects]")
{
    // The corners are a distance of 1 for any roundness, so the falloff there is 1 - intensity x smoothstep(radius,
    // radius + smoothness, 1): with a radius of 0.4 and a smoothness of 1, t = 0.6 and smoothstep = 0.36 x 1.8 = 0.648,
    // so an intensity of 0.5 leaves 1 - 0.324 = 0.676.
    VignetteSettings settings;
    settings.intensity = 0.5f;
    settings.radius = 0.4f;
    settings.smoothness = 1.0f;
    for (const float roundness : {0.0f, 1.0f})
    {
        settings.roundness = roundness;
        for (const Vec2 corner : {Vec2(0.0f), Vec2(1.0f, 0.0f), Vec2(0.0f, 1.0f), Vec2(1.0f)})
        {
            const float falloff = Fenrir::Render::VignetteFalloff(settings, corner, 2.0f);
            INFO("roundness " << roundness << ": the corner (" << corner.x << ", " << corner.y << ") is multiplied by "
                              << falloff);
            REQUIRE(Near(falloff, 0.676f));
        }
    }
}

TEST_CASE("A round vignette falls off alike at the same distance across and down, a square one by the view's shape",
          "[renderer][camera-effects]")
{
    // A view twice as wide as tall. Its top edge's midpoint is a quarter of the view's width from the centre; so is the
    // point a quarter of the way in from the left on the middle row. Round, they are the same distance out: 0.5 of
    // the half-diagonal sqrt(1 + 0.25) in the short side's halves, 0.4472. Roundness 0 measures each axis in its own
    // halves, so the top midpoint is 1 / sqrt(2) = 0.7071 out and the other point 0.5 / sqrt(2) = 0.3536.
    VignetteSettings settings;
    settings.intensity = 1.0f;
    settings.radius = 0.0f;
    settings.smoothness = 1.0f;
    const auto expected = [](float distance) { return 1.0f - distance * distance * (3.0f - 2.0f * distance); };
    const Vec2 top(0.5f, 0.0f);
    const Vec2 across(0.25f, 0.5f);

    settings.roundness = 1.0f;
    const float roundTop = Fenrir::Render::VignetteFalloff(settings, top, 2.0f);
    const float roundAcross = Fenrir::Render::VignetteFalloff(settings, across, 2.0f);
    INFO("round: the top midpoint is multiplied by " << roundTop << ", the point across by " << roundAcross);
    REQUIRE(Near(roundTop, expected(0.4472136f), 1e-4f));
    REQUIRE(Near(roundAcross, expected(0.4472136f), 1e-4f));

    settings.roundness = 0.0f;
    const float squareTop = Fenrir::Render::VignetteFalloff(settings, top, 2.0f);
    const float squareAcross = Fenrir::Render::VignetteFalloff(settings, across, 2.0f);
    INFO("by the view's shape: the top midpoint is multiplied by " << squareTop << ", the point across by "
                                                                   << squareAcross);
    REQUIRE(Near(squareTop, expected(0.7071068f), 1e-4f));
    REQUIRE(Near(squareAcross, expected(0.3535534f), 1e-4f));
}

TEST_CASE("The vignette changes nothing at an intensity of 0", "[renderer][camera-effects]")
{
    VignetteSettings settings;
    settings.radius = 0.0f;
    for (const Vec2 uv : {Vec2(0.0f), Vec2(0.1f, 0.9f), Vec2(1.0f)})
    {
        const float falloff = Fenrir::Render::VignetteFalloff(settings, uv, 1.5f);
        INFO("(" << uv.x << ", " << uv.y << ") is multiplied by " << falloff);
        REQUIRE(falloff == 1.0f);
    }
}

TEST_CASE("The grain's noise is the same for the same pixel and frame", "[renderer][camera-effects]")
{
    for (const std::uint32_t frame : {0u, 1u, 977u})
    {
        const float first = Fenrir::Render::GrainNoise(37, 12, frame);
        const float second = Fenrir::Render::GrainNoise(37, 12, frame);
        INFO("frame " << frame << ": the noise was " << first << ", then " << second);
        REQUIRE(first == second);
        REQUIRE(first >= -1.0f);
        REQUIRE(first < 1.0f);
    }
}

TEST_CASE("The grain's noise differs between frames and between neighbouring pixels", "[renderer][camera-effects]")
{
    // Over a row of 64 pixels, almost every one changes from one frame to the next and from its neighbour.
    int sameAcrossFrames = 0;
    int sameAsNeighbour = 0;
    for (std::uint32_t x = 0; x < 64; ++x)
    {
        sameAcrossFrames += Fenrir::Render::GrainNoise(x, 5, 10) == Fenrir::Render::GrainNoise(x, 5, 11) ? 1 : 0;
        sameAsNeighbour += Fenrir::Render::GrainNoise(x, 5, 10) == Fenrir::Render::GrainNoise(x + 1, 5, 10) ? 1 : 0;
    }
    INFO(sameAcrossFrames << " of 64 pixels kept their noise into the next frame, " << sameAsNeighbour
                          << " matched their right-hand neighbour");
    REQUIRE(sameAcrossFrames <= 1);
    REQUIRE(sameAsNeighbour <= 1);
}

TEST_CASE("The grain's noise averages to about nothing", "[renderer][camera-effects]")
{
    // Two uniform hashes summed into a triangle on [-1, 1): over 4096 pixels the mean is within a few hundredths of 0.
    double sum = 0.0;
    for (std::uint32_t y = 0; y < 64; ++y)
    {
        for (std::uint32_t x = 0; x < 64; ++x)
        {
            sum += Fenrir::Render::GrainNoise(x, y, 3);
        }
    }
    const double mean = sum / 4096.0;
    INFO("the mean noise was " << mean);
    REQUIRE(std::abs(mean) < 0.03);
}

TEST_CASE("The grain shows most in the mid-tones and least in black and white", "[renderer][camera-effects]")
{
    // With a response of 1 the grain's weight is 4 L (1 - L) of the encoded luminance L: none at black and white,
    // all of it at mid grey. The same pixel's noise moves each by its share.
    FilmGrainSettings settings;
    settings.intensity = 0.2f;
    settings.response = 1.0f;
    const float noise = Fenrir::Render::GrainNoise(3, 4, 7);
    {
        INFO("the pixel's noise is 0, so it cannot show the grain's weight");
        REQUIRE(noise != 0.0f);
    }
    const Vec3 black = Fenrir::Render::ApplyFilmGrain(settings, Vec3(0.0f), 3, 4, 7);
    const Vec3 white = Fenrir::Render::ApplyFilmGrain(settings, Vec3(1.0f), 3, 4, 7);
    {
        INFO("black became " << black.r << " and white " << white.r);
        REQUIRE(black == Vec3(0.0f));
        REQUIRE(white == Vec3(1.0f));
    }
    // Mid grey in the encoding, 0.5, is 0.5^2.2 linear; the grain moves its encoding by 0.2 x noise.
    const float grey = std::pow(0.5f, 2.2f);
    const Vec3 grained = Fenrir::Render::ApplyFilmGrain(settings, Vec3(grey), 3, 4, 7);
    const float expected = std::pow(0.5f + 0.2f * noise, 2.2f);
    INFO("mid grey became " << grained.r << ", not " << expected << " (noise " << noise << ")");
    REQUIRE(Near(grained.r, expected, 1e-4f));
    REQUIRE(grained.r == grained.g);
    REQUIRE(grained.g == grained.b);
}

TEST_CASE("Larger grain shares its noise across a cell of pixels", "[renderer][camera-effects]")
{
    // A size of 3: pixels 0 to 2 on each axis are one cell, 3 to 5 the next.
    FilmGrainSettings settings;
    settings.intensity = 0.1f;
    settings.response = 0.0f;
    settings.size = 3.0f;
    const Vec3 grey(0.2f);
    const Vec3 first = Fenrir::Render::ApplyFilmGrain(settings, grey, 0, 0, 2);
    const Vec3 sameCell = Fenrir::Render::ApplyFilmGrain(settings, grey, 2, 2, 2);
    const Vec3 nextCell = Fenrir::Render::ApplyFilmGrain(settings, grey, 3, 0, 2);
    INFO("pixel (0, 0) became " << first.r << ", (2, 2) " << sameCell.r << ", (3, 0) " << nextCell.r);
    REQUIRE(first == sameCell);
    REQUIRE(first != nextCell);
}

TEST_CASE("Film grain changes nothing at an intensity of 0", "[renderer][camera-effects]")
{
    const FilmGrainSettings settings;
    for (const Vec3 colour : {Vec3(0.0f), Vec3(0.18f), Vec3(0.9f, 0.4f, 0.1f)})
    {
        const Vec3 grained = Fenrir::Render::ApplyFilmGrain(settings, colour, 10, 20, 30);
        INFO("(" << colour.r << ", " << colour.g << ", " << colour.b << ") became (" << grained.r << ", " << grained.g
                 << ", " << grained.b << ")");
        REQUIRE(grained == colour);
    }
}
