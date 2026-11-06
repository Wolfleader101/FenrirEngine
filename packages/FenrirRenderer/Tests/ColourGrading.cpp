// The Output pass's colour grade on the CPU (ADR-0059, lighting ticket 26): the C++ mirror of builtin:output's white
// balance, contrast, split toning, saturation and ASC CDL, each against what its definition says it must leave alone or
// produce, worked by hand.

#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "Core/ColourGrading.hpp"
#include "FenrirMath/Math.hpp"

using Fenrir::Math::Vec3;
using Fenrir::Render::ColourGradingSettings;
using Fenrir::Render::GradeColour;

namespace
{
    bool Near(Vec3 a, Vec3 b, float tolerance = 1e-4f)
    {
        return glm::all(glm::lessThan(glm::abs(a - b), Vec3(tolerance)));
    }

    float Luminance(Vec3 colour)
    {
        return 0.2126f * colour.r + 0.7152f * colour.g + 0.0722f * colour.b;
    }

    void RequireGrade(const ColourGradingSettings& settings, Vec3 input, Vec3 expected, float tolerance = 1e-4f)
    {
        const Vec3 actual = GradeColour(settings, input);
        INFO("(" << input.r << ", " << input.g << ", " << input.b << ") grades to (" << actual.r << ", " << actual.g
                 << ", " << actual.b << "), not (" << expected.r << ", " << expected.g << ", " << expected.b << ")");
        REQUIRE(Near(actual, expected, tolerance));
    }
} // namespace

TEST_CASE("The identity grade returns its input exactly", "[renderer][grading]")
{
    const ColourGradingSettings identity;
    {
        INFO("the default settings are the identity");
        REQUIRE(Fenrir::Render::IsIdentity(identity));
    }
    for (const Vec3 colour : {Vec3(0.0f), Vec3(0.18f), Vec3(4.0f, 1.0f, 0.25f), Vec3(-0.01f, 0.3f, 37.5f)})
    {
        const Vec3 graded = GradeColour(identity, colour);
        INFO("(" << colour.r << ", " << colour.g << ", " << colour.b << ") came back as (" << graded.r << ", "
                 << graded.g << ", " << graded.b << ")");
        REQUIRE(graded.r == colour.r);
        REQUIRE(graded.g == colour.g);
        REQUIRE(graded.b == colour.b);
    }
}

TEST_CASE("Any setting off its default makes the grade not identity", "[renderer][grading]")
{
    INFO("the settings changed in the failing line still count as the identity");
    const auto changed = [](auto change) {
        ColourGradingSettings settings;
        change(settings);
        return !Fenrir::Render::IsIdentity(settings);
    };
    REQUIRE(changed([](ColourGradingSettings& s) { s.temperature = 5000.0f; }));
    REQUIRE(changed([](ColourGradingSettings& s) { s.tint = 0.2f; }));
    REQUIRE(changed([](ColourGradingSettings& s) { s.contrast = 1.2f; }));
    REQUIRE(changed([](ColourGradingSettings& s) { s.shadowTint = Vec3(0.2f, 0.6f, 0.7f); }));
    REQUIRE(changed([](ColourGradingSettings& s) { s.highlightTint = Vec3(0.9f, 0.3f, 0.8f); }));
    REQUIRE(changed([](ColourGradingSettings& s) { s.saturation = 0.5f; }));
    REQUIRE(changed([](ColourGradingSettings& s) { s.slope = Vec3(1.0f, 1.1f, 1.0f); }));
    REQUIRE(changed([](ColourGradingSettings& s) { s.offset = Vec3(0.0f, 0.0f, 0.01f); }));
    REQUIRE(changed([](ColourGradingSettings& s) { s.power = Vec3(0.9f, 1.0f, 1.0f); }));
}

TEST_CASE("6500 K and no tint leave D65 white white", "[renderer][grading]")
{
    // Baked and applied directly: GradeColour skips an identity grade, and this holds the matrix itself.
    const Fenrir::Render::ColourGrade grade = Fenrir::Render::BakeColourGrade(ColourGradingSettings{});
    for (const Vec3 colour : {Vec3(1.0f), Vec3(0.18f), Vec3(0.9f, 0.2f, 0.05f)})
    {
        const Vec3 graded = Fenrir::Render::ApplyColourGrade(grade, colour);
        INFO("(" << colour.r << ", " << colour.g << ", " << colour.b << ") became (" << graded.r << ", " << graded.g
                 << ", " << graded.b << ")");
        REQUIRE(Near(graded, colour, 2e-4f));
    }
}

TEST_CASE("A white lit by the daylight the temperature names comes out grey", "[renderer][grading]")
{
    // CIE illuminants D50 and D75, whose published chromaticities (0.34567, 0.35850) and (0.29902, 0.31485) are
    // converted to linear Rec. 709 at a luminance of 1 by IEC 61966-2-1's matrix. Adapted from them to D65, each is
    // grey: the same in every channel, to the accuracy of the daylight formula the temperature is read through.
    struct Case
    {
        float temperature;
        Vec3 white;
    };
    for (const Case& c :
         {Case{5000.0f, Vec3(1.175964f, 0.975724f, 0.722036f)}, Case{7500.0f, Vec3(0.928997f, 1.006441f, 1.145394f)}})
    {
        ColourGradingSettings settings;
        settings.temperature = c.temperature;
        const Vec3 graded = GradeColour(settings, c.white);
        const float mean = (graded.r + graded.g + graded.b) / 3.0f;
        INFO(c.temperature << " K: its white became (" << graded.r << ", " << graded.g << ", " << graded.b << ")");
        REQUIRE(Near(graded, Vec3(mean), 2e-3f));
        // Von Kries keeps the white's luminance close; CAT02 does not keep it exactly.
        INFO("its luminance became " << Luminance(graded));
        REQUIRE(std::abs(Luminance(graded) - 1.0f) < 0.05f);
    }
}

TEST_CASE("A lower temperature cools the picture and a higher one warms it", "[renderer][grading]")
{
    ColourGradingSettings cool;
    cool.temperature = 4000.0f;
    ColourGradingSettings warm;
    warm.temperature = 9000.0f;
    const Vec3 cooled = GradeColour(cool, Vec3(0.5f));
    const Vec3 warmed = GradeColour(warm, Vec3(0.5f));
    INFO("grey at 4000 K became (" << cooled.r << ", " << cooled.g << ", " << cooled.b << "), at 9000 K (" << warmed.r
                                   << ", " << warmed.g << ", " << warmed.b << ")");
    REQUIRE(cooled.b > cooled.r);
    REQUIRE(warmed.r > warmed.b);
}

TEST_CASE("A positive tint names a greener white, so the picture moves to magenta", "[renderer][grading]")
{
    ColourGradingSettings magenta;
    magenta.tint = 0.5f;
    ColourGradingSettings green;
    green.tint = -0.5f;
    const Vec3 a = GradeColour(magenta, Vec3(0.5f));
    const Vec3 b = GradeColour(green, Vec3(0.5f));
    INFO("grey at tint 0.5 became (" << a.r << ", " << a.g << ", " << a.b << "), at -0.5 (" << b.r << ", " << b.g
                                     << ", " << b.b << ")");
    REQUIRE(a.g < a.r);
    REQUIRE(a.g < a.b);
    REQUIRE(b.g > b.r);
    REQUIRE(b.g > b.b);
}

TEST_CASE("Contrast keeps mid grey at 0.18 and steepens the curve in log2 around it", "[renderer][grading]")
{
    for (const float contrast : {0.5f, 1.6f})
    {
        ColourGradingSettings settings;
        settings.contrast = contrast;
        RequireGrade(settings, Vec3(0.18f), Vec3(0.18f));
    }
    // One stop over mid grey, at a contrast of 1.5, lands 1.5 stops over it: 0.18 x 2^1.5.
    ColourGradingSettings steeper;
    steeper.contrast = 1.5f;
    RequireGrade(steeper, Vec3(0.36f), Vec3(0.509117f));
    // Two stops under lands three under: 0.18 x 2^-3.
    RequireGrade(steeper, Vec3(0.045f), Vec3(0.0225f));
}

TEST_CASE("Grey split tints change nothing, whatever their luminance and the balance", "[renderer][grading]")
{
    ColourGradingSettings plain;
    plain.contrast = 1.3f;
    ColourGradingSettings grey = plain;
    grey.shadowTint = Vec3(0.5f);
    grey.highlightTint = Vec3(3.0f);
    grey.balance = 0.4f;
    for (const Vec3 colour : {Vec3(0.002f), Vec3(0.18f), Vec3(0.9f, 0.2f, 0.05f), Vec3(12.0f, 3.0f, 1.0f)})
    {
        RequireGrade(grey, colour, GradeColour(plain, colour), 1e-6f);
    }
}

namespace
{
    // The neon city's tints: teal shadows, magenta highlights.
    const Vec3 Teal(0.1f, 0.6f, 0.7f);
    const Vec3 Magenta(0.9f, 0.1f, 0.7f);

    ColourGradingSettings SplitToned(float balance)
    {
        ColourGradingSettings settings;
        settings.shadowTint = Teal;
        settings.highlightTint = Magenta;
        settings.balance = balance;
        return settings;
    }
} // namespace

TEST_CASE("A split-toned mid grey is tinted and keeps its luminance", "[renderer][grading]")
{
    for (const float balance : {-0.5f, 0.0f, 0.5f})
    {
        const Vec3 graded = GradeColour(SplitToned(balance), Vec3(0.18f));
        INFO("balance " << balance << ": mid grey became (" << graded.r << ", " << graded.g << ", " << graded.b << ")");
        REQUIRE(std::abs(Luminance(graded) - 0.18f) < 1e-4f);
        REQUIRE_FALSE(Near(graded, Vec3(0.18f), 1e-3f));
    }
}

TEST_CASE("Shadows take the shadow tint and highlights the highlight tint", "[renderer][grading]")
{
    // Six stops under and over mid grey, past where the tints cross: each takes one tint, at unit luminance.
    const ColourGradingSettings settings = SplitToned(0.0f);
    const Vec3 dark(0.18f / 64.0f);
    const Vec3 bright(0.18f * 64.0f);
    RequireGrade(settings, dark, dark * Teal / Luminance(Teal), 1e-5f);
    RequireGrade(settings, bright, bright * Magenta / Luminance(Magenta), 1e-2f);
}

TEST_CASE("A positive balance gives the highlights' tint more of the picture", "[renderer][grading]")
{
    // At mid grey, the teal's green against the magenta's.
    const Vec3 shadowward = GradeColour(SplitToned(-0.5f), Vec3(0.18f));
    const Vec3 highlightward = GradeColour(SplitToned(0.5f), Vec3(0.18f));
    INFO("mid grey at balance -0.5 became (" << shadowward.r << ", " << shadowward.g << ", " << shadowward.b
                                             << "), at 0.5 (" << highlightward.r << ", " << highlightward.g << ", "
                                             << highlightward.b << ")");
    REQUIRE(highlightward.g < shadowward.g);
    REQUIRE(highlightward.r > shadowward.r);
}

TEST_CASE("Saturation 0 gives grey of the input's luminance, and fades the split tints too", "[renderer][grading]")
{
    ColourGradingSettings grey;
    grey.saturation = 0.0f;
    // 0.2126 x 0.9 + 0.7152 x 0.2 + 0.0722 x 0.05.
    RequireGrade(grey, Vec3(0.9f, 0.2f, 0.05f), Vec3(0.33799f));

    ColourGradingSettings toned = SplitToned(0.0f);
    toned.saturation = 0.0f;
    RequireGrade(toned, Vec3(0.18f), Vec3(0.18f));
}

TEST_CASE("Saturation scales each colour's distance from its luminance", "[renderer][grading]")
{
    ColourGradingSettings richer;
    richer.saturation = 1.5f;
    // Luminance 0.3353; each channel 1.5 times as far from it.
    RequireGrade(richer, Vec3(0.5f, 0.3f, 0.2f), Vec3(0.58235f, 0.28235f, 0.13235f));
}

TEST_CASE("The CDL matches its formula: (in x slope + offset), negatives clamped, to the power", "[renderer][grading]")
{
    ColourGradingSettings cdl;
    cdl.slope = Vec3(1.2f, 1.0f, 0.8f);
    cdl.offset = Vec3(0.02f, -0.05f, 0.1f);
    cdl.power = Vec3(1.1f, 1.0f, 2.2f);
    // 0.62^1.1, 0.15^1 and 0.18^2.2.
    RequireGrade(cdl, Vec3(0.5f, 0.2f, 0.1f), Vec3(0.591059f, 0.15f, 0.022993f));
    // Green's 0.02 - 0.05 is clamped to 0 before its power.
    RequireGrade(cdl, Vec3(0.5f, 0.02f, 0.1f), Vec3(0.591059f, 0.0f, 0.022993f));
}
