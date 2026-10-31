// Bloom on OpenGL (ADR-0049): pixel probes of SceneColor after the BloomComposite, whose bloom spreads a bright spot
// evenly every way and keeps its energy, with and without MSAA; and a golden image of a glowing emissive sphere. The
// spot is the 2 x 2 pixels round the 64 x 64 window's centre, the smallest spot every level of the chain halves evenly,
// so its bloom is mirror-symmetric about that centre (a lone pixel sits off-centre in its 2 x 2 box at every level).
// Run the GL tests with FENRIR_UPDATE_GOLDENS=1 to write the golden again, after a change meant to alter it.

#include <algorithm>
#include <cmath>
#include <cstddef>
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

    /** @brief The light a spot of `value` on its 2 x 2 pixels holds: the sum of a channel over them. */
    double SpotEnergy(float value)
    {
        return 4.0 * value;
    }

    /**
     * @brief A black scene with an unlit spot of `value` (each channel) on its centre 2 x 2 pixels, seen by an
     * orthographic camera one world unit a pixel with bloom at `intensity` and `radius`. Returns the camera.
     */
    Fenrir::Camera& AddSpot(GlScene& test, float value, float intensity, float radius)
    {
        Fenrir::Camera& camera = test.AddCamera();
        camera.projection = Fenrir::Orthographic{static_cast<float>(Size), 0.1f, 100.0f};
        camera.background = Fenrir::ColourBackground{Fenrir::Colour::Black};
        camera.bloom.intensity = intensity;
        camera.bloom.radius = radius;
        test.AddQuad(2.0f, 1.0f, "builtin:unlit", Fenrir::Colour{value, value, value, 1.0f});
        return camera;
    }

    /** @brief One frame of AddSpot's scene; its SceneColor after the bloom. */
    GlProbe::Image DrawSpot(GlScene& test, float value, float intensity, float radius)
    {
        AddSpot(test, value, intensity, radius);
        return test.Frame(Size, Size);
    }

    /** @brief The red channel at `x`, `y`: the spot is grey, so every channel is the same. */
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

    /** @brief Whether `a` and `b` agree to within half floats' precision and a little filtering error. */
    bool Close(float a, float b)
    {
        return std::abs(a - b) <= 2e-3f * std::max(std::abs(a), std::abs(b)) + 1e-6f;
    }
} // namespace

TEST_CASE("A bright spot's bloom spreads evenly every way from it", "[renderer][gl][bloom]")
{
    GlScene test;
    test.RequireOpenGL();
    // Bloom alone (intensity 1), so the spot's own pixels hold their share of the bloom only.
    const GlProbe::Image frame = DrawSpot(test, 64.0f, 1.0f, 0.5f);
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());

    constexpr std::uint32_t c = Size / 2;
    INFO("the light reaches well past the spot, falling away from it");
    REQUIRE(Red(frame, c + 8, c) > 0.0f);
    for (std::uint32_t d = 0; d < 12; ++d)
    {
        REQUIRE(Red(frame, c + d, c) >= Red(frame, c + d + 1, c));
    }
    INFO("mirrored left and right, up and down, and across the diagonal, the bloom is the same");
    for (std::uint32_t y = 0; y < Size; ++y)
    {
        for (std::uint32_t x = 0; x < Size; ++x)
        {
            const float here = Red(frame, x, y);
            INFO("at (" << x << ", " << y << "): " << here << "; mirrored across: " << Red(frame, Size - 1 - x, y)
                        << ", down: " << Red(frame, x, Size - 1 - y) << ", the diagonal: " << Red(frame, y, x));
            REQUIRE(Close(here, Red(frame, Size - 1 - x, y)));
            REQUIRE(Close(here, Red(frame, x, Size - 1 - y)));
            REQUIRE(Close(here, Red(frame, y, x)));
        }
    }
}

TEST_CASE("Bloom keeps a dim spot's energy, mixed in at any intensity", "[renderer][gl][bloom]")
{
    // Dim, so the Karis average on the first downsample, 1 / (1 + luma), takes almost nothing.
    constexpr float Value = 0.05f;
    const double spot = SpotEnergy(Value);
    for (const float intensity : {1.0f, 0.5f})
    {
        for (const float radius : {0.2f, 0.7f})
        {
            GlScene test;
            test.RequireOpenGL();
            const GlProbe::Image frame = DrawSpot(test, Value, intensity, radius);
            INFO("no GL error or other error was logged");
            REQUIRE(test.messages->errors.empty());
            const double energy = Energy(frame);
            INFO("at intensity " << intensity << " and radius " << radius << " the frame holds " << energy
                                 << " of the spot's " << spot);
            REQUIRE(std::abs(energy - spot) < 0.01 * spot);
            INFO("and spreads it: the spot's own pixels keep less than they had");
            REQUIRE(Red(frame, Size / 2, Size / 2) < Value);
        }
    }
}

TEST_CASE("Under MSAA bloom resolves SceneColor and still keeps the spot's energy", "[renderer][gl][bloom][msaa]")
{
    constexpr float Value = 0.05f;
    GlScene test(4);
    test.RequireOpenGL();
    Fenrir::AssetStore& store = test.assets.Store();
    Fenrir::ImageData data;
    data.width = Size;
    data.height = Size;
    const auto image = store.Add<Fenrir::Image>(std::move(data), Fenrir::AssetOwner::App());
    // Read after Output, which resolves the multisampled SceneColor: six stops over and through None, so the bloom's
    // dim tails show in the 8-bit sRGB target and nothing reaches 1.
    Fenrir::Camera& camera = AddSpot(test, Value, 1.0f, 0.7f);
    camera.target = Fenrir::ImageTarget{image};
    camera.tonemapper = Fenrir::Tonemapper::None;
    camera.exposure = 6.0f;
    test.RunFrame();
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());

    const RendererTest::Rgba8Image frame = test.ReadTarget(image);
    double energy = 0.0;
    for (std::size_t i = 0; i < frame.rgba.size(); i += 4)
    {
        const double stored = frame.rgba[i] / 255.0;
        const double linear = stored <= 0.04045 ? stored / 12.92 : std::pow((stored + 0.055) / 1.055, 2.4);
        energy += linear / 64.0;
    }
    const double spot = SpotEnergy(Value);
    INFO("the frame holds " << energy << " of the spot's " << spot << ", read through an 8-bit sRGB target");
    REQUIRE(std::abs(energy - spot) < 0.03 * spot);
}

TEST_CASE("An emissive sphere glowing with bloom matches its golden image", "[renderer][gl][bloom][golden]")
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
    // The default camera's bloom and AgX, over a dark blue.
    Fenrir::Camera& camera = test.AddCamera();
    camera.bloom = Fenrir::Bloom{};
    camera.target = Fenrir::ImageTarget{image};
    camera.background = Fenrir::ColourBackground{Fenrir::Colour{0.01f, 0.012f, 0.02f, 1.0f}};

    // A black lit sphere glowing orange at 24 times its emissive colour, beside a dim grey one it lights nothing on.
    test.app.GetActiveScene().AddResource<Fenrir::Sky>(Fenrir::ColourSky{Fenrir::Colour{0.05f, 0.05f, 0.05f, 1.0f}});
    const auto sphere = store.Add<Fenrir::Mesh>(RendererTest::Sphere(), Fenrir::AssetOwner::App());
    const auto glowing = store.CreateMaterial(
        store.Load<Fenrir::Shader>("builtin:lit", Fenrir::AssetOwner::App()).handle, Fenrir::AssetOwner::App());
    store.SetColour(glowing, "baseColour", Fenrir::Colour::Black);
    store.SetColour(glowing, "emissive", {1.0f, 0.35f, 0.08f, 1.0f});
    store.SetFloat(glowing, "emissiveStrength", 24.0f);
    const auto grey = store.CreateMaterial(store.Load<Fenrir::Shader>("builtin:lit", Fenrir::AssetOwner::App()).handle,
                                           Fenrir::AssetOwner::App());
    store.SetColour(grey, "baseColour", {0.5f, 0.5f, 0.5f, 1.0f});
    for (const auto& [material, x] : {std::pair{glowing, -0.9f}, std::pair{grey, 1.1f}})
    {
        Fenrir::Entity entity = test.app.GetActiveScene().GetEntityList().CreateEntity();
        entity.GetComponent<Fenrir::Transform>().pos = Vec3(x, 0.0f, -4.0f);
        entity.GetComponent<Fenrir::Transform>().scale = Vec3(1.5f);
        entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{sphere, material, true});
    }

    test.RunFrame();
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());
    RendererTest::RequireGolden("bloom-emissive-sphere", test.ReadTarget(image), FENRIR_TEST_GOLDENS,
                                FENRIR_TEST_GOLDEN_FAILURES);
}
