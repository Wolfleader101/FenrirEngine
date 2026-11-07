// The Output pass's chromatic aberration, vignette and film grain on OpenGL (ADR-0059, lighting ticket 25): pixel
// probes of each effect against what it must do and against the C++ mirrors in Core/CameraEffects.hpp, the frame with
// every effect at 0 against the tonemap goldens, and golden images of each effect alone over the tonemapping golden
// scene (GoldenScene.hpp), compared with the committed PNGs in OpenGL/goldens under RendererTest::GoldenTolerance. Run
// the GL tests with FENRIR_UPDATE_GOLDENS=1 to write the goldens again, after a change meant to alter them; look at
// each before committing it.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "Core/CameraEffects.hpp"
#include "Core/Tonemapping.hpp"
#include "GlScene.hpp"
#include "GoldenImages.hpp"
#include "GoldenScene.hpp"
#include "RendererProbe.hpp"

using Fenrir::GlProbe;
using Fenrir::RendererProbe;
using Fenrir::Math::Vec2;
using Fenrir::Math::Vec3;
using GlTest::DrawGoldenScene;
using GlTest::GlScene;

namespace
{
    float Encode(float linear)
    {
        return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    }

    /** @brief The core's copy of `vignette`, as the facade makes it. */
    Fenrir::Render::VignetteSettings ToCore(const Fenrir::Vignette& vignette)
    {
        return {vignette.intensity, vignette.radius, vignette.smoothness, vignette.roundness};
    }

    /** @brief The core's copy of `grain`, as the facade makes it. */
    Fenrir::Render::FilmGrainSettings ToCore(const Fenrir::FilmGrain& grain)
    {
        return {grain.intensity, grain.response, grain.size};
    }

    /** @brief A camera drawing into a new `width` x `height` image, its background `colour`, through None. */
    Fenrir::AssetHandle<Fenrir::Image> FlatView(GlScene& test, Fenrir::Camera& camera, std::uint32_t width,
                                                std::uint32_t height, Fenrir::Colour colour)
    {
        Fenrir::ImageData data;
        data.width = width;
        data.height = height;
        const auto image = test.assets.Store().Add<Fenrir::Image>(std::move(data), Fenrir::AssetOwner::App());
        camera.target = Fenrir::ImageTarget{image};
        camera.background = Fenrir::ColourBackground{colour};
        camera.tonemapper = Fenrir::Tonemapper::None;
        return image;
    }

    /** @brief Each effect at a strength that shows in the golden scene. */
    void Aberrated(Fenrir::Camera& camera)
    {
        camera.chromaticAberration.intensity = 0.04f;
    }

    void Vignetted(Fenrir::Camera& camera)
    {
        camera.vignette = {0.5f, 0.3f, 0.7f, 1.0f};
    }

    void Grained(Fenrir::Camera& camera)
    {
        camera.filmGrain = {0.08f, 0.8f, 1.0f};
    }

    /** @brief Every effect on, strongly. */
    void EveryEffect(Fenrir::Camera& camera)
    {
        camera.chromaticAberration.intensity = 0.1f;
        camera.vignette = {1.0f, 0.1f, 0.5f, 0.5f};
        camera.filmGrain = {0.3f, 0.2f, 2.0f};
    }

    void RequireGolden(std::string_view name, const RendererTest::Rgba8Image& frame)
    {
        RendererTest::RequireGolden(name, frame, FENRIR_TEST_GOLDENS, FENRIR_TEST_GOLDEN_FAILURES);
    }

    /** @brief An unlit quad of `colour`, `width` x `height`, centred at (`x`, 0) four units in front of the camera. */
    void AddPatch(GlScene& test, float x, float width, float height, Fenrir::Colour colour)
    {
        Fenrir::AssetStore& store = test.assets.Store();
        const auto material = store.CreateMaterial(
            store.Load<Fenrir::Shader>("builtin:unlit", Fenrir::AssetOwner::App()).handle, Fenrir::AssetOwner::App());
        store.SetColour(material, "baseColour", colour);
        const auto quad = store.Add<Fenrir::Mesh>(RendererTest::Quad(1.0f), Fenrir::AssetOwner::App());
        Fenrir::Entity patch = test.app.GetActiveScene().GetEntityList().CreateEntity();
        Fenrir::Transform& transform = patch.GetComponent<Fenrir::Transform>();
        transform.pos = Vec3(x, 0.0f, -4.0f);
        transform.scale = Vec3(width, height, 1.0f);
        patch.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{quad, material, true});
    }

    /**
     * @brief Over black, through None, with `msaa` samples and chromatic aberration of `aberration`, into a 95 x 63
     * image, so one pixel's centre is the view's: a thin white bar standing right of the centre, and an orange square
     * over the centre. Read back.
     */
    GlProbe::Image AberrationFrame(std::uint32_t msaa, float aberration)
    {
        GlScene test(msaa);
        test.RequireOpenGL();
        Fenrir::Camera& camera = test.AddCamera();
        const auto image = FlatView(test, camera, 95, 63, Fenrir::Colour::Black);
        camera.chromaticAberration.intensity = aberration;
        AddPatch(test, 2.0f, 0.3f, 3.0f, Fenrir::Colour::White);
        AddPatch(test, 0.0f, 1.2f, 1.2f, Fenrir::Colour{1.0f, 0.5f, 0.2f, 1.0f});

        test.RunFrame();
        INFO("no GL error or other error was logged");
        REQUIRE(test.messages->errors.empty());
        return GlProbe::Read(RendererProbe::Device(test.renderer), RendererProbe::ImageTexture(test.renderer, image));
    }
} // namespace

TEST_CASE("Chromatic aberration fringes an edge red outside and blue inside", "[renderer][gl][camera-effects]")
{
    for (const std::uint32_t msaa : {1u, 4u})
    {
        // Without aberration, the bar's white columns on the middle row; with it, red shows past its outer edge and
        // blue before its inner one. 0.1 of the half-diagonal moves the bar's edges, some 25 pixels out, over 2, so
        // the pixel beside each edge reads well inside the bar.
        const GlProbe::Image plain = AberrationFrame(msaa, 0.0f);
        std::uint32_t inner = plain.width;
        std::uint32_t outer = 0;
        for (std::uint32_t x = 60; x < plain.width; ++x)
        {
            if (plain.At(x, 31)[0] > 0.5f)
            {
                inner = std::min(inner, x);
                outer = std::max(outer, x);
            }
        }
        INFO(msaa << " samples: the bar covers columns " << inner << " to " << outer);
        REQUIRE(inner > 60);
        REQUIRE(outer < 93);

        const GlProbe::Image fringed = AberrationFrame(msaa, 0.1f);
        const GlProbe::Pixel outside = fringed.At(outer + 1, 31);
        const GlProbe::Pixel inside = fringed.At(inner - 1, 31);
        INFO("the pixel outside the bar: (" << outside[0] << ", " << outside[1] << ", " << outside[2]
                                            << "); the one inside it: (" << inside[0] << ", " << inside[1] << ", "
                                            << inside[2] << ")");
        REQUIRE(outside[0] > 0.9f);
        REQUIRE(outside[2] < 0.1f);
        REQUIRE(inside[2] > 0.9f);
        REQUIRE(inside[0] < 0.1f);
        INFO("green is read where it is, so it stays inside the bar");
        REQUIRE(outside[1] < 0.1f);
        REQUIRE(inside[1] < 0.1f);
    }
}

TEST_CASE("Chromatic aberration leaves the view's centre as it is", "[renderer][gl][camera-effects]")
{
    for (const std::uint32_t msaa : {1u, 4u})
    {
        // Pixel (47, 31)'s centre is the view's, so every channel is read there; the orange square's edges move.
        const GlProbe::Image plain = AberrationFrame(msaa, 0.0f);
        const GlProbe::Image fringed = AberrationFrame(msaa, 0.1f);
        const GlProbe::Pixel before = plain.At(47, 31);
        const GlProbe::Pixel after = fringed.At(47, 31);
        INFO(msaa << " samples: the centre was (" << before[0] << ", " << before[1] << ", " << before[2]
                  << "), became (" << after[0] << ", " << after[1] << ", " << after[2] << ")");
        REQUIRE(after == before);
        bool edgeMoved = false;
        for (std::uint32_t x = 30; x < 65; ++x)
        {
            edgeMoved = edgeMoved || plain.At(x, 31) != fringed.At(x, 31);
        }
        INFO("no pixel of the middle row across the square changed");
        REQUIRE(edgeMoved);
    }
}

TEST_CASE("The vignette darkens a flat view by its CPU falloff", "[renderer][gl][camera-effects]")
{
    // A flat 0.18 through None: each pixel is 0.18 times the mirror's falloff at its centre, sRGB-encoded.
    for (const float roundness : {0.0f, 1.0f})
    {
        GlScene test;
        test.RequireOpenGL();
        Fenrir::Camera& camera = test.AddCamera();
        const auto image = FlatView(test, camera, 96, 64, Fenrir::Colour{0.18f, 0.18f, 0.18f, 1.0f});
        camera.vignette = {0.6f, 0.2f, 0.6f, roundness};
        test.RunFrame();
        const GlProbe::Image pixels =
            GlProbe::Read(RendererProbe::Device(test.renderer), RendererProbe::ImageTexture(test.renderer, image));

        for (const auto [x, y] : {std::pair{0u, 0u}, std::pair{95u, 0u}, std::pair{0u, 63u}, std::pair{95u, 63u},
                                  std::pair{48u, 0u}, std::pair{0u, 32u}, std::pair{20u, 10u}, std::pair{48u, 32u}})
        {
            const Vec2 uv((static_cast<float>(x) + 0.5f) / 96.0f, (static_cast<float>(y) + 0.5f) / 64.0f);
            const float expected =
                Encode(0.18f * Fenrir::Render::VignetteFalloff(ToCore(camera.vignette), uv, 96.0f / 64.0f));
            const GlProbe::Pixel actual = pixels.At(x, y);
            INFO("roundness " << roundness << ", pixel (" << x << ", " << y << "): the GPU stored " << actual[0]
                              << ", the mirror encodes to " << expected);
            REQUIRE(GlProbe::Near(actual, {expected, expected, expected, 1.0f}));
        }
        INFO("the corners are darker than the centre");
        REQUIRE(pixels.At(0, 0)[0] < pixels.At(48, 32)[0] - 0.1f);
    }
}

TEST_CASE("Film grain matches its CPU mirror for the frame it is drawn in", "[renderer][gl][camera-effects]")
{
    // A flat 0.18 through None, grained: every pixel against the mirror's grain for its pixel and the frame's index.
    for (const float size : {1.0f, 2.5f})
    {
        GlScene test;
        test.RequireOpenGL();
        Fenrir::Camera& camera = test.AddCamera();
        const auto image = FlatView(test, camera, 32, 16, Fenrir::Colour{0.18f, 0.18f, 0.18f, 1.0f});
        camera.filmGrain = {0.1f, 0.8f, size};
        const auto frame = static_cast<std::uint32_t>(RendererProbe::Core(test.renderer).FrameIndex());
        test.RunFrame();
        const GlProbe::Image pixels =
            GlProbe::Read(RendererProbe::Device(test.renderer), RendererProbe::ImageTexture(test.renderer, image));

        float lowest = 1.0f;
        float highest = 0.0f;
        for (std::uint32_t y = 0; y < 16; ++y)
        {
            for (std::uint32_t x = 0; x < 32; ++x)
            {
                const Vec3 grained = Fenrir::Render::ApplyFilmGrain(ToCore(camera.filmGrain), Vec3(0.18f), x, y, frame);
                const float expected = Encode(grained.r);
                const GlProbe::Pixel actual = pixels.At(x, y);
                INFO("size " << size << ", frame " << frame << ", pixel (" << x << ", " << y << "): the GPU stored "
                             << actual[0] << ", the mirror encodes to " << expected);
                REQUIRE(GlProbe::Near(actual, {expected, expected, expected, 1.0f}));
                lowest = std::min(lowest, actual[0]);
                highest = std::max(highest, actual[0]);
            }
        }
        INFO("the grain spans " << lowest << " to " << highest);
        REQUIRE(highest - lowest > 0.05f);
    }
}

TEST_CASE("With every effect at 0 the frame is the tonemap golden exactly", "[renderer][gl][golden]")
{
    // Every other setting off its default, so only the intensities keep the effects off: through each tonemapper and
    // exposure the tonemap goldens cover, the same bytes as the plain frame, and its golden.
    const auto off = [](Fenrir::Camera& camera) {
        camera.chromaticAberration.intensity = 0.0f;
        camera.vignette = {0.0f, 0.1f, 0.2f, 0.3f};
        camera.filmGrain = {0.0f, 0.1f, 3.0f};
    };
    struct Golden
    {
        std::string_view name;
        Fenrir::Tonemapper tonemapper;
        float exposure;
    };
    for (const Golden& golden :
         {Golden{"tonemap-none", Fenrir::Tonemapper::None, 0.0f}, Golden{"tonemap-agx", Fenrir::Tonemapper::AgX, 0.0f},
          Golden{"tonemap-pbr-neutral", Fenrir::Tonemapper::PbrNeutral, 0.0f},
          Golden{"tonemap-aces", Fenrir::Tonemapper::Aces, 0.0f},
          Golden{"tonemap-agx-minus-2ev", Fenrir::Tonemapper::AgX, -2.0f},
          Golden{"tonemap-agx-plus-2ev", Fenrir::Tonemapper::AgX, 2.0f}})
    {
        const RendererTest::Rgba8Image plain = DrawGoldenScene(golden.tonemapper, golden.exposure);
        const RendererTest::Rgba8Image zeroed = DrawGoldenScene(golden.tonemapper, golden.exposure, off);
        RequireGolden(golden.name, zeroed);
        INFO(golden.name << ": the frame with every effect at 0 differs from the plain frame in some byte");
        REQUIRE(zeroed.rgba == plain.rgba);
    }
}

TEST_CASE("A grey debug view shows its input without the camera's effects", "[renderer][gl][camera-effects]")
{
    // The ambient occlusion view, with and without every effect: the same bytes.
    const auto occlusion = [](Fenrir::Camera& camera) {
        camera.ambientOcclusion.enabled = true;
        camera.debugView = Fenrir::CameraDebugView::AmbientOcclusion;
    };
    const RendererTest::Rgba8Image plain = DrawGoldenScene(Fenrir::Tonemapper::AgX, 0.0f, occlusion);
    const RendererTest::Rgba8Image affected = DrawGoldenScene(Fenrir::Tonemapper::AgX, 0.0f, [&](Fenrir::Camera& c) {
        occlusion(c);
        EveryEffect(c);
    });
    INFO("the ambient occlusion view changed under the camera's effects");
    REQUIRE(affected.rgba == plain.rgba);
}

TEST_CASE("Each camera effect changes the lit scene", "[renderer][gl][golden]")
{
    // Against the golden comparison itself, so an effect silently skipped fails here too.
    const RendererTest::Rgba8Image plain = DrawGoldenScene(Fenrir::Tonemapper::AgX, 0.0f);
    const auto changed = [&](void (*effect)(Fenrir::Camera&)) {
        return !RendererTest::CompareWithGolden(plain, DrawGoldenScene(Fenrir::Tonemapper::AgX, 0.0f, effect))
                    .Matches({});
    };
    {
        INFO("chromatic aberration left the scene as it was");
        REQUIRE(changed(Aberrated));
    }
    {
        INFO("the vignette left the scene as it was");
        REQUIRE(changed(Vignetted));
    }
    {
        INFO("film grain left the scene as it was");
        REQUIRE(changed(Grained));
    }
}

TEST_CASE("The lit scene through AgX with chromatic aberration matches its golden image", "[renderer][gl][golden]")
{
    RequireGolden("tonemap-agx-aberration", DrawGoldenScene(Fenrir::Tonemapper::AgX, 0.0f, Aberrated));
}

TEST_CASE("The lit scene through AgX with a vignette matches its golden image", "[renderer][gl][golden]")
{
    RequireGolden("tonemap-agx-vignette", DrawGoldenScene(Fenrir::Tonemapper::AgX, 0.0f, Vignetted));
}

TEST_CASE("The lit scene through AgX with film grain matches its golden image", "[renderer][gl][golden]")
{
    // A fresh scene's first frame, so the grain's frame index is the same every run.
    RequireGolden("tonemap-agx-grain", DrawGoldenScene(Fenrir::Tonemapper::AgX, 0.0f, Grained));
}
