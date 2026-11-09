// Lens flare and lens dirt on OpenGL (ADR-0059): pixel probes of SceneColor after the FlareComposite, where a bright
// spot off the centre throws a ghost on the far side of it, on the line through it, and a spot at the centre lights a
// halo ring; lens dirt that is black leaves bloom as it was, and white doubles its share. And golden images of a small
// emissive sphere off the centre over black, flaring, without and with the generated lens dirt (never a fetched map).
// Run the GL tests with FENRIR_UPDATE_GOLDENS=1 to write the goldens again, after a change meant to alter them.

#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirRenderer/LensDirt.hpp"
#include "GlScene.hpp"
#include "GoldenImages.hpp"

using Fenrir::GlProbe;
using Fenrir::Math::Vec3;
using GlTest::GlScene;

namespace
{
    constexpr std::uint32_t Size = GlScene::Size;
    constexpr std::uint32_t Centre = Size / 2;

    /**
     * @brief An orthographic camera one world unit a pixel over black, its bloom at `bloom` and its flare at
     * `intensity`, with no halo and no fringe, and an unlit 2 x 2 spot of `value` (each channel) centred `offset`
     * pixels right of the view's centre. Returns the camera.
     */
    Fenrir::Camera& AddSpot(GlScene& test, float value, float offset, float intensity, float bloom)
    {
        Fenrir::Camera& camera = test.AddCamera();
        camera.projection = Fenrir::Orthographic{static_cast<float>(Size), 0.1f, 100.0f};
        camera.background = Fenrir::ColourBackground{Fenrir::Colour::Black};
        camera.bloom.intensity = bloom;
        camera.lensFlare.intensity = intensity;
        camera.lensFlare.haloWidth = 0.0f;
        camera.lensFlare.chromaticOffset = 0.0f;

        Fenrir::AssetStore& store = test.assets.Store();
        const auto material = store.CreateMaterial(
            store.Load<Fenrir::Shader>("builtin:unlit", Fenrir::AssetOwner::App()).handle, Fenrir::AssetOwner::App());
        store.SetColour(material, "baseColour", Fenrir::Colour{value, value, value, 1.0f});
        const auto mesh = store.Add<Fenrir::Mesh>(RendererTest::Quad(2.0f, -1.0f), Fenrir::AssetOwner::App());
        Fenrir::Entity entity = test.app.GetActiveScene().GetEntityList().CreateEntity();
        entity.GetComponent<Fenrir::Transform>().pos = Vec3(offset, 0.0f, 0.0f);
        entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{mesh, material, true});
        return camera;
    }

    float Red(const GlProbe::Image& frame, std::uint32_t x, std::uint32_t y)
    {
        return frame.At(x, y)[0];
    }

    /** @brief A flat, linear 4 x 4 image, every pixel `value`. */
    Fenrir::AssetHandle<Fenrir::Image> AddDirt(GlScene& test, std::uint8_t value)
    {
        return test.assets.Store().Add<Fenrir::Image>(
            RendererTest::Picture(4, 4, std::vector<std::uint8_t>(64, value), Fenrir::ColourSpace::Linear),
            Fenrir::AssetOwner::App());
    }

    /**
     * @brief A 96 x 64 render target over black, the default bloom and AgX, a flare a fifth of the mix, and a small
     * emissive sphere up and to the right of the centre; `dirt` on the lens when it is not empty. Returns the target.
     */
    Fenrir::AssetHandle<Fenrir::Image> FlareGoldenScene(GlScene& test, bool dirty)
    {
        constexpr std::uint32_t Width = 96;
        constexpr std::uint32_t Height = 64;
        Fenrir::AssetStore& store = test.assets.Store();
        Fenrir::ImageData data;
        data.width = Width;
        data.height = Height;
        const auto image = store.Add<Fenrir::Image>(std::move(data), Fenrir::AssetOwner::App());
        Fenrir::Camera& camera = test.AddCamera();
        camera.bloom = Fenrir::Bloom{};
        camera.target = Fenrir::ImageTarget{image};
        camera.background = Fenrir::ColourBackground{Fenrir::Colour::Black};
        camera.lensFlare.intensity = 0.2f;
        camera.lensFlare.chromaticOffset = 0.03f;
        if (dirty)
        {
            camera.lensFlare.dirt =
                store.Add<Fenrir::Image>(Fenrir::GenerateLensDirt(Width, Height, 3), Fenrir::AssetOwner::App());
            camera.lensFlare.dirtIntensity = 6.0f;
        }

        const auto sphere = store.Add<Fenrir::Mesh>(RendererTest::Sphere(), Fenrir::AssetOwner::App());
        const auto glowing = store.CreateMaterial(
            store.Load<Fenrir::Shader>("builtin:lit", Fenrir::AssetOwner::App()).handle, Fenrir::AssetOwner::App());
        store.SetColour(glowing, "baseColour", Fenrir::Colour::Black);
        store.SetColour(glowing, "emissive", Fenrir::Colour::White);
        store.SetFloat(glowing, "emissiveStrength", 200.0f);
        Fenrir::Entity entity = test.app.GetActiveScene().GetEntityList().CreateEntity();
        entity.GetComponent<Fenrir::Transform>().pos = Vec3(1.2f, 0.6f, -6.0f);
        entity.GetComponent<Fenrir::Transform>().scale = Vec3(0.25f);
        entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{sphere, glowing, true});
        return image;
    }
} // namespace

TEST_CASE("A bright spot off the centre throws a ghost on the far side of it, on the line through it",
          "[renderer][gl][flare]")
{
    GlScene test;
    test.RequireOpenGL();
    // The flare alone (intensity 1, no bloom), the spot 12 pixels right of the centre. At the default spacing, 0.4,
    // its fourth ghost lands 12 / (1 - 1.6) = 20 pixels left of the centre.
    AddSpot(test, 64.0f, 12.0f, 1.0f, 0.0f);
    const GlProbe::Image frame = test.Frame(Size, Size);
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());

    const float ghost = Red(frame, Centre - 20, Centre);
    const float offLine = Red(frame, Centre, Centre - 20);
    const float aboveGhost = Red(frame, Centre - 20, Centre - 12);
    INFO("the ghost is " << ghost << "; as far from the centre off the line, " << offLine
                         << "; 12 pixels above the "
                            "ghost, "
                         << aboveGhost);
    REQUIRE(ghost > 0.0f);
    REQUIRE(ghost > 4.0f * offLine);
    // That ghost is magnified 1 / 0.6 times, so it is broad: it falls by more than half in 12 pixels.
    REQUIRE(ghost > 2.0f * aboveGhost);
}

TEST_CASE("A bright spot at the centre lights a halo ring at the halo's radius", "[renderer][gl][flare]")
{
    GlScene test;
    test.RequireOpenGL();
    Fenrir::Camera& camera = AddSpot(test, 64.0f, 0.0f, 1.0f, 0.0f);
    camera.lensFlare.ghostCount = 0;
    camera.lensFlare.haloRadius = 0.7f;
    camera.lensFlare.haloWidth = 0.15f;
    const GlProbe::Image frame = test.Frame(Size, Size);
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());

    // A square view's corners are 32 * sqrt(2) pixels out, so the ring is 0.7 of that, about 31.7 pixels, from the
    // spot's centre (between the centre four pixels): along the diagonals, 22.4 pixels across and up.
    const float right = Red(frame, Centre - 1 + 23, Centre - 1 + 23);
    const float left = Red(frame, Centre - 23, Centre - 23);
    const float inside = Red(frame, Centre - 1 + 15, Centre - 1 + 15);
    INFO("on the ring " << right << " and " << left << "; nearer the centre, between it and the spot, " << inside);
    REQUIRE(right > 0.0f);
    REQUIRE(std::abs(right - left) < 0.25f * right);
    REQUIRE(right > 2.0f * inside);
}

TEST_CASE("Lens dirt that is black leaves bloom as it was, and white doubles its share", "[renderer][gl][flare]")
{
    constexpr float Value = 4.0f;
    constexpr float Bloom = 0.2f;
    const auto frameWith = [](int dirt, float dirtIntensity) {
        GlScene test;
        test.RequireOpenGL();
        Fenrir::Camera& camera = AddSpot(test, Value, 0.0f, 0.0f, Bloom);
        if (dirt >= 0)
        {
            camera.lensFlare.dirt = AddDirt(test, static_cast<std::uint8_t>(dirt));
            camera.lensFlare.dirtIntensity = dirtIntensity;
        }
        GlProbe::Image frame = test.Frame(Size, Size);
        INFO("no GL error or other error was logged");
        REQUIRE(test.messages->errors.empty());
        return frame;
    };
    const GlProbe::Image clean = frameWith(-1, 0.0f);
    const GlProbe::Image black = frameWith(0, 5.0f);
    const GlProbe::Image white = frameWith(255, 1.0f);

    INFO("black dirt at any intensity is a clean lens");
    for (std::uint32_t y = 0; y < Size; ++y)
    {
        for (std::uint32_t x = 0; x < Size; ++x)
        {
            REQUIRE(std::abs(Red(black, x, y) - Red(clean, x, y)) <= 1e-6f + 1e-4f * Red(clean, x, y));
        }
    }
    INFO("white dirt at intensity 1 doubles the bloom's share, so off the spot, where all the light is the bloom's, "
         "each pixel holds twice as much");
    for (const std::uint32_t d : {4u, 8u, 12u})
    {
        const float before = Red(clean, Centre + d, Centre);
        const float after = Red(white, Centre + d, Centre);
        INFO(d << " pixels right of the spot: " << before << " clean, " << after << " dirty");
        REQUIRE(before > 0.0f);
        REQUIRE(std::abs(after - 2.0f * before) < 0.01f * before);
    }
}

TEST_CASE("A small emissive sphere flaring over black matches its golden image", "[renderer][gl][flare][golden]")
{
    GlScene test;
    test.RequireOpenGL();
    const auto image = FlareGoldenScene(test, false);
    test.RunFrame();
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());
    RendererTest::RequireGolden("flare-emissive-point", test.ReadTarget(image), FENRIR_TEST_GOLDENS,
                                FENRIR_TEST_GOLDEN_FAILURES);
}

TEST_CASE("A small emissive sphere flaring through generated lens dirt matches its golden image",
          "[renderer][gl][flare][golden]")
{
    GlScene test;
    test.RequireOpenGL();
    const auto image = FlareGoldenScene(test, true);
    test.RunFrame();
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());
    RendererTest::RequireGolden("flare-emissive-point-dirt", test.ReadTarget(image), FENRIR_TEST_GOLDENS,
                                FENRIR_TEST_GOLDEN_FAILURES);
}
