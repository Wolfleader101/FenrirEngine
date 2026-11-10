// Anamorphic streaks on OpenGL (ADR-0059): pixel probes of SceneColor after the StreakComposite, whose streak runs
// from a bright spot far further across than up and down and keeps the frame's energy, as bloom does; and a golden
// image of a small emissive sphere over black, streaking blue. The spot is the 2 x 2 pixels round the 64 x 64 window's
// centre, as bloom's tests have it. Run the GL tests with FENRIR_UPDATE_GOLDENS=1 to write the golden again, after a
// change meant to alter it.

#include <cmath>
#include <cstdint>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "GlScene.hpp"
#include "GoldenImages.hpp"

using Fenrir::GlProbe;
using Fenrir::Math::Vec3;
using GlTest::GlScene;

namespace
{
    constexpr std::uint32_t Size = GlScene::Size;

    /**
     * @brief A black scene with an unlit spot of `value` (each channel) on its centre 2 x 2 pixels, seen by an
     * orthographic camera one world unit a pixel with streaks at `intensity` and `length` and bloom at
     * `bloomIntensity`. Returns the camera.
     */
    Fenrir::Camera& AddSpot(GlScene& test, float value, float intensity, float length, float bloomIntensity)
    {
        Fenrir::Camera& camera = test.AddCamera();
        camera.projection = Fenrir::Orthographic{static_cast<float>(Size), 0.1f, 100.0f};
        camera.background = Fenrir::ColourBackground{Fenrir::Colour::Black};
        camera.look.bloom.intensity = bloomIntensity;
        camera.look.anamorphicStreaks.intensity = intensity;
        camera.look.anamorphicStreaks.length = length;
        test.AddQuad(2.0f, 1.0f, "builtin:unlit", Fenrir::Colour{value, value, value, 1.0f});
        return camera;
    }

    float Red(const GlProbe::Image& frame, std::uint32_t x, std::uint32_t y)
    {
        return frame.At(x, y)[0];
    }

    /** @brief The sum of the red channel over the whole frame: the light it holds. */
    double Energy(const GlProbe::Image& frame)
    {
        double sum = 0.0;
        for (std::uint32_t y = 0; y < frame.height; ++y)
        {
            for (std::uint32_t x = 0; x < frame.width; ++x)
            {
                sum += Red(frame, x, y);
            }
        }
        return sum;
    }
} // namespace

TEST_CASE("A bright spot's streak reaches far further across than up and down", "[renderer][gl][streaks]")
{
    GlScene test;
    test.RequireOpenGL();
    // The streak alone (intensity 1, no bloom), so every lit pixel is the streak's.
    AddSpot(test, 64.0f, 1.0f, 0.7f, 0.0f);
    const GlProbe::Image frame = test.Frame(Size, Size);
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());

    constexpr std::uint32_t c = Size / 2;
    // How far from the spot's centre, along its row or its column, the streak is still a thousandth of its peak.
    const float peak = Red(frame, c, c);
    INFO("the spot's own pixels hold some of its streak");
    REQUIRE(peak > 0.0f);
    std::uint32_t across = 0;
    std::uint32_t upDown = 0;
    for (std::uint32_t d = 1; d < c; ++d)
    {
        if (Red(frame, c + d, c) > 1e-3f * peak)
        {
            across = d;
        }
        if (Red(frame, c, c + d) > 1e-3f * peak)
        {
            upDown = d;
        }
    }
    INFO("the streak reaches " << across << " pixels across and " << upDown << " up and down");
    REQUIRE(across >= 24);
    REQUIRE(upDown <= 6);
    REQUIRE(across >= 4 * upDown);
    INFO("it falls away from the spot along its row, and is the same either side of it");
    for (std::uint32_t d = 0; d < 20; ++d)
    {
        REQUIRE(Red(frame, c + d, c) >= Red(frame, c + d + 1, c));
        REQUIRE(std::abs(Red(frame, c + d, c) - Red(frame, c - 1 - d, c)) <= 2e-3f * Red(frame, c + d, c) + 1e-6f);
    }
}

TEST_CASE("Streaks keep a dim spot's energy, mixed in at any intensity and length, with bloom or without",
          "[renderer][gl][streaks]")
{
    // Dim, so the Karis average on the first downsample takes almost nothing; bloom's test's tolerance.
    constexpr float Value = 0.05f;
    const double spot = 4.0 * Value;
    for (const float bloom : {0.0f, 0.04f})
    {
        for (const float intensity : {1.0f, 0.3f})
        {
            for (const float length : {0.2f, 0.8f})
            {
                GlScene test;
                test.RequireOpenGL();
                AddSpot(test, Value, intensity, length, bloom);
                const GlProbe::Image frame = test.Frame(Size, Size);
                INFO("no GL error or other error was logged");
                REQUIRE(test.messages->errors.empty());
                const double energy = Energy(frame);
                INFO("bloom " << bloom << ", streaks at intensity " << intensity << " and length " << length
                              << ": the frame holds " << energy << " of the spot's " << spot);
                REQUIRE(std::abs(energy - spot) < 0.01 * spot);
                INFO("and the streak reaches 12 pixels across");
                REQUIRE(Red(frame, Size / 2 + 12, Size / 2) > 0.0f);
            }
        }
    }
}

TEST_CASE("A tinted streak keeps the frame's luminance", "[renderer][gl][streaks]")
{
    constexpr float Value = 0.05f;
    GlScene test;
    test.RequireOpenGL();
    Fenrir::Camera& camera = AddSpot(test, Value, 0.5f, 0.7f, 0.0f);
    camera.look.anamorphicStreaks.tint = Fenrir::Colour{0.2f, 0.4f, 1.0f, 1.0f};
    const GlProbe::Image frame = test.Frame(Size, Size);
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());

    double luminance = 0.0;
    for (std::uint32_t y = 0; y < frame.height; ++y)
    {
        for (std::uint32_t x = 0; x < frame.width; ++x)
        {
            const auto texel = frame.At(x, y);
            luminance += 0.2126 * texel[0] + 0.7152 * texel[1] + 0.0722 * texel[2];
        }
    }
    const double spot = 4.0 * Value;
    INFO("the frame's luminance is " << luminance << " of the spot's " << spot);
    REQUIRE(std::abs(luminance - spot) < 0.01 * spot);
    INFO("and the streak off the spot is blue");
    const auto streak = frame.At(Size / 2 + 12, Size / 2);
    REQUIRE(streak[2] > streak[1]);
    REQUIRE(streak[1] > streak[0]);
}

TEST_CASE("A small emissive sphere streaking blue over black matches its golden image",
          "[renderer][gl][streaks][golden]")
{
    constexpr std::uint32_t Width = 96;
    constexpr std::uint32_t Height = 64;
    GlScene test;
    test.RequireOpenGL();
    Fenrir::AssetStore& store = test.assets.Store();
    Fenrir::ImageData data;
    data.width = Width;
    data.height = Height;
    const auto image = store.Add<Fenrir::Image>(std::move(data), Fenrir::AssetOwner::App());
    // The default bloom and AgX, with streaks a fifth of the mix, long and blue.
    Fenrir::Camera& camera = test.AddCamera();
    camera.target = Fenrir::ImageTarget{image};
    camera.background = Fenrir::ColourBackground{Fenrir::Colour::Black};
    camera.look.anamorphicStreaks = {0.2f, 0.8f, Fenrir::Colour{0.3f, 0.55f, 1.0f, 1.0f}};

    const auto sphere = store.Add<Fenrir::Mesh>(RendererTest::Sphere(), Fenrir::AssetOwner::App());
    const auto glowing = store.CreateMaterial(
        store.Load<Fenrir::Shader>("builtin:lit", Fenrir::AssetOwner::App()).handle, Fenrir::AssetOwner::App());
    store.SetColour(glowing, "baseColour", Fenrir::Colour::Black);
    store.SetColour(glowing, "emissive", Fenrir::Colour::White);
    store.SetFloat(glowing, "emissiveStrength", 200.0f);
    Fenrir::Entity entity = test.app.GetActiveScene().GetEntityList().CreateEntity();
    entity.GetComponent<Fenrir::Transform>().pos = Vec3(0.0f, 0.0f, -6.0f);
    entity.GetComponent<Fenrir::Transform>().scale = Vec3(0.25f);
    entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{sphere, glowing, true});

    test.RunFrame();
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());
    RendererTest::RequireGolden("streaks-emissive-point", test.ReadTarget(image), FENRIR_TEST_GOLDENS,
                                FENRIR_TEST_GOLDEN_FAILURES);
}
