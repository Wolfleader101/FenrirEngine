// The Output pass's exposure, colour grade and tonemappers on OpenGL (ADR-0049, ADR-0059): flat colours through each
// tonemapper, and through each control of the grade, held to the C++ mirrors in Core/Tonemapping.hpp and
// Core/ColourGrading.hpp, and golden images: one lit scene with highlights well over 1, drawn into an image target
// through each tonemapper, at three exposures and under one grade, and compared with the committed PNG in
// OpenGL/goldens under RendererTest::GoldenTolerance. Run the GL tests with FENRIR_UPDATE_GOLDENS=1 to write the
// goldens again, after a change meant to alter them; look at each before committing it.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "Core/ColourGrading.hpp"
#include "Core/Tonemapping.hpp"
#include "GlScene.hpp"
#include "GoldenImages.hpp"
#include "GoldenScene.hpp"

using Fenrir::GlProbe;
using Fenrir::RendererProbe;
using Fenrir::Math::Vec3;
using GlTest::DrawGoldenScene;
using GlTest::GlScene;

namespace
{
    /**
     * @brief A grade with every control off its default: warmer, a little magenta, more contrast, teal shadows and
     * magenta highlights split a stop under mid grey, a little more saturation and a CDL trim.
     */
    Fenrir::ColourGrading NonTrivialGrade()
    {
        Fenrir::ColourGrading grading;
        grading.temperature = 7500.0f;
        grading.tint = 0.15f;
        grading.contrast = 1.3f;
        grading.shadowTint = Fenrir::Colour{0.1f, 0.6f, 0.7f, 1.0f};
        grading.highlightTint = Fenrir::Colour{0.9f, 0.3f, 0.7f, 1.0f};
        grading.balance = 0.25f;
        grading.saturation = 1.15f;
        grading.slope = Vec3(1.05f, 1.0f, 0.95f);
        grading.offset = Vec3(0.0f, 0.0f, 0.005f);
        grading.power = Vec3(1.0f, 1.05f, 1.0f);
        return grading;
    }

    /** @brief The core's copy of `grading`, as the facade makes it. */
    Fenrir::Render::ColourGradingSettings ToCore(const Fenrir::ColourGrading& grading)
    {
        Fenrir::Render::ColourGradingSettings settings;
        settings.temperature = grading.temperature;
        settings.tint = grading.tint;
        settings.contrast = grading.contrast;
        settings.shadowTint = Vec3(grading.shadowTint.r, grading.shadowTint.g, grading.shadowTint.b);
        settings.highlightTint = Vec3(grading.highlightTint.r, grading.highlightTint.g, grading.highlightTint.b);
        settings.balance = grading.balance;
        settings.saturation = grading.saturation;
        settings.slope = grading.slope;
        settings.offset = grading.offset;
        settings.power = grading.power;
        return settings;
    }

    /** @brief Sets a camera's grade to NonTrivialGrade. */
    void Graded(Fenrir::Camera& camera)
    {
        camera.colourGrading = NonTrivialGrade();
    }

    void RequireGolden(std::string_view name, const RendererTest::Rgba8Image& frame)
    {
        RendererTest::RequireGolden(name, frame, FENRIR_TEST_GOLDENS, FENRIR_TEST_GOLDEN_FAILURES);
    }
} // namespace

TEST_CASE("The lit scene through None matches its golden image, its highlights clipped", "[renderer][gl][golden]")
{
    RequireGolden("tonemap-none", DrawGoldenScene(Fenrir::Tonemapper::None, 0.0f));
}

TEST_CASE("The lit scene through AgX matches its golden image", "[renderer][gl][golden]")
{
    RequireGolden("tonemap-agx", DrawGoldenScene(Fenrir::Tonemapper::AgX, 0.0f));
}

TEST_CASE("The lit scene through PBR Neutral matches its golden image", "[renderer][gl][golden]")
{
    RequireGolden("tonemap-pbr-neutral", DrawGoldenScene(Fenrir::Tonemapper::PbrNeutral, 0.0f));
}

TEST_CASE("The lit scene through ACES matches its golden image", "[renderer][gl][golden]")
{
    RequireGolden("tonemap-aces", DrawGoldenScene(Fenrir::Tonemapper::Aces, 0.0f));
}

TEST_CASE("The lit scene through AgX two stops under matches its golden image", "[renderer][gl][golden]")
{
    RequireGolden("tonemap-agx-minus-2ev", DrawGoldenScene(Fenrir::Tonemapper::AgX, -2.0f));
}

TEST_CASE("The lit scene through AgX two stops over matches its golden image", "[renderer][gl][golden]")
{
    RequireGolden("tonemap-agx-plus-2ev", DrawGoldenScene(Fenrir::Tonemapper::AgX, 2.0f));
}

TEST_CASE("The lit scene through AgX under a grade matches its golden image", "[renderer][gl][golden]")
{
    RequireGolden("tonemap-agx-graded", DrawGoldenScene(Fenrir::Tonemapper::AgX, 0.0f, Graded));
}

TEST_CASE("A grade changes the lit scene", "[renderer][gl][golden]")
{
    // Against the golden comparison itself, so a grade silently skipped fails here too.
    const RendererTest::Rgba8Image plain = DrawGoldenScene(Fenrir::Tonemapper::AgX, 0.0f);
    const RendererTest::Rgba8Image graded = DrawGoldenScene(Fenrir::Tonemapper::AgX, 0.0f, Graded);
    INFO("the graded scene matches the plain one under the golden comparison");
    REQUIRE_FALSE(RendererTest::CompareWithGolden(plain, graded).Matches({}));
}

TEST_CASE("Output's colour grade matches its C++ mirror, through the tonemapper", "[renderer][gl][grading]")
{
    // As for the tonemappers below: a view of nothing but one flat linear colour, graded by each control on its own
    // and by all of them, after the exposure and before the tonemapper, against the mirror's colour sRGB-encoded.
    std::array<Fenrir::ColourGrading, 9> gradings{};
    gradings[0].temperature = 4000.0f;
    gradings[1].temperature = 12000.0f;
    gradings[2].tint = -0.4f;
    gradings[3].contrast = 1.6f;
    gradings[4].shadowTint = Fenrir::Colour{0.1f, 0.6f, 0.7f, 1.0f};
    gradings[4].highlightTint = Fenrir::Colour{0.9f, 0.1f, 0.7f, 1.0f};
    gradings[5] = gradings[4];
    gradings[5].balance = -0.6f;
    gradings[6].saturation = 0.3f;
    gradings[7].slope = Vec3(1.2f, 1.0f, 0.8f);
    gradings[7].offset = Vec3(0.02f, -0.05f, 0.1f);
    gradings[7].power = Vec3(1.1f, 1.0f, 2.2f);
    gradings[8] = NonTrivialGrade();
    const std::array<Vec3, 4> colours{Vec3(0.18f), Vec3(0.02f, 0.015f, 0.01f), Vec3(4.0f, 1.0f, 0.25f),
                                      Vec3(0.05f, 0.4f, 1.5f)};
    // None at half a stop under shows the grade itself, clamped; AgX at a stop over, through a curve.
    struct Mapping
    {
        Fenrir::Tonemapper tonemapper;
        float exposure;
    };
    constexpr std::array<Mapping, 2> mappings{Mapping{Fenrir::Tonemapper::None, -0.5f},
                                              Mapping{Fenrir::Tonemapper::AgX, 1.0f}};
    const auto encode = [](float linear) {
        return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    };
    for (std::size_t g = 0; g < gradings.size(); ++g)
    {
        for (const Vec3 colour : colours)
        {
            for (const Mapping& mapping : mappings)
            {
                GlScene test;
                test.RequireOpenGL();
                Fenrir::ImageData data;
                data.width = 4;
                data.height = 4;
                const auto image = test.assets.Store().Add<Fenrir::Image>(std::move(data), Fenrir::AssetOwner::App());
                Fenrir::Camera& camera = test.AddCamera();
                camera.target = Fenrir::ImageTarget{image};
                camera.background = Fenrir::ColourBackground{Fenrir::Colour{colour.r, colour.g, colour.b, 1.0f}};
                camera.tonemapper = mapping.tonemapper;
                camera.exposure = mapping.exposure;
                camera.colourGrading = gradings[g];

                test.RunFrame();
                const GlProbe::Image pixels = GlProbe::Read(RendererProbe::Device(test.renderer),
                                                            RendererProbe::ImageTexture(test.renderer, image));

                const Vec3 graded = Fenrir::Render::GradeColour(
                    ToCore(gradings[g]), colour * Fenrir::Render::ExposureScale(mapping.exposure));
                const Vec3 expected =
                    Fenrir::Render::Tonemap(static_cast<Fenrir::Render::Tonemapper>(mapping.tonemapper), graded);
                const GlProbe::Pixel actual = pixels.At(2, 2);
                INFO("grading " << g << ", tonemapper " << static_cast<int>(mapping.tonemapper) << ", colour ("
                                << colour.r << ", " << colour.g << ", " << colour.b << ") at " << mapping.exposure
                                << " EV: the GPU stored (" << actual[0] << ", " << actual[1] << ", " << actual[2]
                                << "), the mirror encodes to (" << encode(expected.r) << ", " << encode(expected.g)
                                << ", " << encode(expected.b) << ")");
                REQUIRE(GlProbe::Near(actual, {encode(expected.r), encode(expected.g), encode(expected.b), 1.0f}));
            }
        }
    }
}

TEST_CASE("Output's tonemappers and exposure match their C++ mirror", "[renderer][gl][tonemapping]")
{
    // A view of nothing but its background: one flat linear colour through Output, read back as the sRGB image
    // stores it, against the mirror's colour encoded the same way.
    constexpr std::array<Fenrir::Tonemapper, 4> tonemappers{Fenrir::Tonemapper::None, Fenrir::Tonemapper::AgX,
                                                            Fenrir::Tonemapper::PbrNeutral, Fenrir::Tonemapper::Aces};
    const std::array<Vec3, 4> colours{Vec3(0.18f), Vec3(1.0f), Vec3(4.0f, 1.0f, 0.25f), Vec3(0.05f, 0.4f, 1.5f)};
    constexpr std::array<float, 3> exposures{0.0f, -1.5f, 2.0f};
    const auto encode = [](float linear) {
        return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    };
    for (const Fenrir::Tonemapper tonemapper : tonemappers)
    {
        for (const Vec3 colour : colours)
        {
            for (const float exposure : exposures)
            {
                GlScene test;
                test.RequireOpenGL();
                Fenrir::ImageData data;
                data.width = 4;
                data.height = 4;
                const auto image = test.assets.Store().Add<Fenrir::Image>(std::move(data), Fenrir::AssetOwner::App());
                Fenrir::Camera& camera = test.AddCamera();
                camera.target = Fenrir::ImageTarget{image};
                camera.background = Fenrir::ColourBackground{Fenrir::Colour{colour.r, colour.g, colour.b, 1.0f}};
                camera.tonemapper = tonemapper;
                camera.exposure = exposure;

                test.RunFrame();
                const GlProbe::Image pixels = GlProbe::Read(RendererProbe::Device(test.renderer),
                                                            RendererProbe::ImageTexture(test.renderer, image));

                const Vec3 expected = Fenrir::Render::Tonemap(static_cast<Fenrir::Render::Tonemapper>(tonemapper),
                                                              colour * Fenrir::Render::ExposureScale(exposure));
                const GlProbe::Pixel actual = pixels.At(2, 2);
                INFO("tonemapper " << static_cast<int>(tonemapper) << ", colour (" << colour.r << ", " << colour.g
                                   << ", " << colour.b << ") at " << exposure << " EV: the GPU stored (" << actual[0]
                                   << ", " << actual[1] << ", " << actual[2] << "), the mirror encodes to ("
                                   << encode(expected.r) << ", " << encode(expected.g) << ", " << encode(expected.b)
                                   << ")");
                REQUIRE(GlProbe::Near(actual, {encode(expected.r), encode(expected.g), encode(expected.b), 1.0f}));
            }
        }
    }
}

TEST_CASE("Each tonemapper draws the lit scene differently", "[renderer][gl][golden]")
{
    // Against the golden comparison itself, so a tonemapper silently drawing as another fails here too.
    const RendererTest::Rgba8Image none = DrawGoldenScene(Fenrir::Tonemapper::None, 0.0f);
    const RendererTest::Rgba8Image agx = DrawGoldenScene(Fenrir::Tonemapper::AgX, 0.0f);
    const RendererTest::Rgba8Image neutral = DrawGoldenScene(Fenrir::Tonemapper::PbrNeutral, 0.0f);
    const RendererTest::Rgba8Image aces = DrawGoldenScene(Fenrir::Tonemapper::Aces, 0.0f);
    const RendererTest::Rgba8Image brighter = DrawGoldenScene(Fenrir::Tonemapper::AgX, 2.0f);
    INFO("no two of None, AgX, PBR Neutral, ACES and AgX at +2 EV match");
    const RendererTest::Rgba8Image* frames[]{&none, &agx, &neutral, &aces, &brighter};
    for (std::size_t i = 0; i < std::size(frames); ++i)
    {
        for (std::size_t j = i + 1; j < std::size(frames); ++j)
        {
            INFO("frames " << i << " and " << j);
            REQUIRE_FALSE(RendererTest::CompareWithGolden(*frames[i], *frames[j]).Matches({}));
        }
    }
}
