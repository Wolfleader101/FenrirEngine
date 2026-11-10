// Ambient occlusion on OpenGL (the spec's seam 2, ADR-0049): GTAO from the depth prepass darkens an inside corner and
// leaves a flat floor alone, read through the camera's CameraDebugView::AmbientOcclusion into an image target; it
// darkens builtin:lit's sky light there and never the sun's; and the sphere grid on a floor against a wall matches its
// golden image.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <tuple>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "GlScene.hpp"

using Fenrir::GlProbe;
using Fenrir::Math::Vec3;
using GlTest::GlScene;

namespace
{
    constexpr std::uint32_t Size = GlScene::Size;

    /** @brief A lit material of `colour`, rough and dielectric. */
    Fenrir::AssetHandle<Fenrir::Material> Lit(GlScene& test, Fenrir::Colour colour)
    {
        Fenrir::AssetStore& store = test.assets.Store();
        const auto material = store.CreateMaterial(
            store.Load<Fenrir::Shader>("builtin:lit", Fenrir::AssetOwner::App()).handle, Fenrir::AssetOwner::App());
        store.SetColour(material, "baseColour", colour);
        store.SetFloat(material, "roughness", 1.0f);
        return material;
    }

    /** @brief A square of side `size` centred on `position`, facing +Z turned by `rotation`. */
    void AddPlane(GlScene& test, float size, Vec3 position, Fenrir::Math::Quat rotation,
                  Fenrir::AssetHandle<Fenrir::Material> material)
    {
        const auto mesh =
            test.assets.Store().Add<Fenrir::Mesh>(RendererTest::Quad(size, 0.0f), Fenrir::AssetOwner::App());
        Fenrir::Entity entity = test.app.GetActiveScene().GetEntityList().CreateEntity();
        entity.GetComponent<Fenrir::Transform>().pos = position;
        entity.GetComponent<Fenrir::Transform>().rot = rotation;
        entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{mesh, material, true});
    }

    /** @brief A floor at y = -1 facing up, from z = 0 to -8. */
    void AddFloor(GlScene& test, Fenrir::AssetHandle<Fenrir::Material> material)
    {
        AddPlane(test, 8.0f, {0.0f, -1.0f, -4.0f},
                 Fenrir::Math::AngleAxis(Fenrir::Math::DegToRad(-90.0f), Vec3(1.0f, 0.0f, 0.0f)), material);
    }

    /** @brief A wall at z = -4 facing the camera, standing on the floor: the two meet in an inside corner. */
    void AddWall(GlScene& test, Fenrir::AssetHandle<Fenrir::Material> material)
    {
        AddPlane(test, 8.0f, {0.0f, 3.0f, -4.0f}, Fenrir::Math::Quat(1.0f, 0.0f, 0.0f, 0.0f), material);
    }

    /** @brief The linear value of an sRGB-encoded byte. */
    float Linear(std::uint8_t encoded)
    {
        const float c = static_cast<float>(encoded) / 255.0f;
        return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }

    /** @brief Pixel (`x`, `y`) of `image`'s red, linear. */
    float RedAt(const RendererTest::Rgba8Image& image, std::uint32_t x, std::uint32_t y)
    {
        return Linear(image.rgba.at((std::size_t{y} * image.width + x) * 4));
    }

    /**
     * @brief Runs a frame with a camera at the origin looking along -Z showing its ambient occlusion into a `Size`
     * square image, and reads it back.
     */
    RendererTest::Rgba8Image OcclusionFrame(GlScene& test)
    {
        Fenrir::AssetStore& store = test.assets.Store();
        Fenrir::ImageData data;
        data.width = Size;
        data.height = Size;
        const auto target = store.Add<Fenrir::Image>(std::move(data), Fenrir::AssetOwner::App());
        Fenrir::Camera& camera = test.AddCamera();
        camera.target = Fenrir::ImageTarget{target};
        camera.ambientOcclusion.enabled = true;
        camera.debugView = Fenrir::CameraDebugView::AmbientOcclusion;
        test.RunFrame();
        return test.ReadTarget(target);
    }
} // namespace

TEST_CASE("GTAO darkens an inside corner more than the floor and the wall away from it", "[renderer][gl][gtao]")
{
    const std::uint32_t msaa = GENERATE(1u, 4u);
    GlScene test(msaa);
    test.RequireOpenGL();
    const auto white = Lit(test, Fenrir::Colour::White);
    AddFloor(test, white);
    AddWall(test, white);

    const RendererTest::Rgba8Image occlusion = OcclusionFrame(test);

    // The corner (y = -1, z = -4) is seen at about row 46; the floor two units nearer at about row 60, and the wall
    // two units up at about row 17: both further from the corner than the effect radius (0.5 x 1.457).
    const float corner = RedAt(occlusion, Size / 2, 45);
    const float floor = RedAt(occlusion, Size / 2, 60);
    const float wall = RedAt(occlusion, Size / 2, 17);
    INFO(msaa << " samples: the occlusion at the corner is " << corner << ", on the floor " << floor
              << " and on the wall " << wall);
    REQUIRE(corner < 0.8f);
    REQUIRE(corner < floor - 0.15f);
    REQUIRE(corner < wall - 0.15f);
    INFO("the floor and the wall away from the corner are unoccluded");
    REQUIRE(floor > 0.95f);
    REQUIRE(wall > 0.95f);
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("GTAO leaves a flat floor alone unoccluded", "[renderer][gl][gtao]")
{
    const std::uint32_t msaa = GENERATE(1u, 4u);
    GlScene test(msaa);
    test.RequireOpenGL();
    AddFloor(test, Lit(test, Fenrir::Colour::White));

    const RendererTest::Rgba8Image occlusion = OcclusionFrame(test);

    // The floor's horizon is at row 32; from a few rows below it down to the bottom, every pixel across.
    float darkest = 1.0f;
    for (std::uint32_t y = 36; y < Size; ++y)
    {
        for (std::uint32_t x = 0; x < Size; ++x)
        {
            darkest = std::min(darkest, RedAt(occlusion, x, y));
        }
    }
    INFO(msaa << " samples: the darkest floor pixel's occlusion is " << darkest);
    REQUIRE(darkest > 0.95f);
    INFO("and the sky above it, where nothing is drawn, too");
    REQUIRE(RedAt(occlusion, Size / 2, 4) == 1.0f);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("GTAO darkens the sky's light in an inside corner, and never the sun's", "[renderer][gl][gtao]")
{
    GlScene test;
    test.RequireOpenGL();
    const bool sky = GENERATE(true, false);
    const auto white = Lit(test, Fenrir::Colour::White);
    AddFloor(test, white);
    AddWall(test, white);
    if (sky)
    {
        test.app.GetActiveScene().AddResource<Fenrir::Sky>(Fenrir::ColourSky{Fenrir::Colour::White});
    }
    else
    {
        // Black sky, and the sun shining down onto the floor and the wall from behind the camera.
        test.app.GetActiveScene().AddResource<Fenrir::Sky>(Fenrir::ColourSky{Fenrir::Colour::Black});
        Fenrir::Entity sun = test.app.GetActiveScene().GetEntityList().CreateEntity();
        sun.GetComponent<Fenrir::Transform>().rot = Fenrir::Math::AngleAxis(0.3f, Vec3(0.0f, 1.0f, 0.0f)) *
                                                    Fenrir::Math::AngleAxis(-0.8f, Vec3(1.0f, 0.0f, 0.0f));
        sun.AddComponent<Fenrir::DirectionalLight>();
    }
    Fenrir::Camera& camera = test.AddCamera();
    camera.look.tonemapper = Fenrir::Tonemapper::None;

    const GlProbe::Image without = test.Frame();
    camera.ambientOcclusion.enabled = true;
    const GlProbe::Image with = test.Frame();

    const GlProbe::Pixel cornerWithout = without.At(Size / 2, 45);
    const GlProbe::Pixel cornerWith = with.At(Size / 2, 45);
    INFO((sky ? "under a white sky" : "under the sun alone")
         << ": the corner's red is " << cornerWithout[0] << " without GTAO and " << cornerWith[0] << " with it");
    REQUIRE(cornerWithout[0] > 0.05f);
    if (sky)
    {
        REQUIRE(cornerWith[0] < cornerWithout[0] * 0.85f);
        // Within the few percent a flat surface's occlusion falls short of 1 (XeGTAO's fast arccosine, raised to 2.2).
        INFO("the floor away from the corner is lit almost as it was: " << without.At(Size / 2, 60)[0] << " without, "
                                                                        << with.At(Size / 2, 60)[0] << " with");
        REQUIRE(GlProbe::Near(with.At(Size / 2, 60), without.At(Size / 2, 60), 0.05f));
    }
    else
    {
        REQUIRE(GlProbe::Near(cornerWith, cornerWithout, 0.002f));
    }
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("The sphere grid on a floor against a wall with GTAO matches its golden image",
          "[renderer][gl][golden][gtao]")
{
    GlScene test;
    test.RequireOpenGL();
    Fenrir::AssetStore& store = test.assets.Store();
    Fenrir::ImageData data;
    data.width = 160;
    data.height = 90;
    const auto target = store.Add<Fenrir::Image>(std::move(data), Fenrir::AssetOwner::App());
    Fenrir::Entity cameraEntity = test.app.GetActiveScene().GetEntityList().CreateEntity();
    Fenrir::Camera& camera = cameraEntity.AddComponent<Fenrir::Camera>();
    camera.look.bloom.intensity = 0.0f;
    camera.target = Fenrir::ImageTarget{target};
    camera.ambientOcclusion.enabled = true;
    // A little above the grid, looking down at it.
    cameraEntity.GetComponent<Fenrir::Transform>().pos = Vec3(0.0f, 0.6f, 0.0f);
    cameraEntity.GetComponent<Fenrir::Transform>().rot = Fenrir::Math::AngleAxis(-0.15f, Vec3(1.0f, 0.0f, 0.0f));

    // A pale sky lighting a grey room, with a soft sun from up and to the right, so the contact shadows show.
    test.app.GetActiveScene().AddResource<Fenrir::Sky>(Fenrir::ColourSky{Fenrir::Colour{0.9f, 0.95f, 1.0f, 1.0f}});
    Fenrir::Entity sun = test.app.GetActiveScene().GetEntityList().CreateEntity();
    sun.GetComponent<Fenrir::Transform>().rot =
        Fenrir::Math::AngleAxis(-0.6f, Vec3(0.0f, 1.0f, 0.0f)) * Fenrir::Math::AngleAxis(-0.9f, Vec3(1.0f, 0.0f, 0.0f));
    // Its shadows off: the golden is GTAO's contact shadows, not the sun's (Shadows.cpp's goldens).
    Fenrir::DirectionalLight& light = sun.AddComponent<Fenrir::DirectionalLight>();
    light.intensity = 0.8f;
    light.castShadows = false;
    const auto grey = Lit(test, Fenrir::Colour{0.6f, 0.6f, 0.6f, 1.0f});
    AddPlane(test, 12.0f, {0.0f, -1.03f, -4.0f},
             Fenrir::Math::AngleAxis(Fenrir::Math::DegToRad(-90.0f), Vec3(1.0f, 0.0f, 0.0f)), grey);
    AddPlane(test, 12.0f, {0.0f, 4.97f, -4.6f}, Fenrir::Math::Quat(1.0f, 0.0f, 0.0f, 0.0f), grey);

    // The top row a red dielectric, the bottom row gold resting on the floor, each from roughness 0 to 1.
    for (int column = 0; column < 5; ++column)
    {
        const float roughness = static_cast<float>(column) / 4.0f;
        const float x = -2.2f + 1.1f * static_cast<float>(column);
        for (const auto& [y, colour, metallic] : {std::tuple{0.5f, Fenrir::Colour{0.8f, 0.1f, 0.08f, 1.0f}, 0.0f},
                                                  std::tuple{-0.55f, Fenrir::Colour{1.0f, 0.78f, 0.34f, 1.0f}, 1.0f}})
        {
            const auto material = Lit(test, colour);
            store.SetFloat(material, "metallic", metallic);
            store.SetFloat(material, "roughness", roughness);
            Fenrir::Entity sphere = test.app.GetActiveScene().GetEntityList().CreateEntity();
            sphere.GetComponent<Fenrir::Transform>().pos = Vec3(x, y, -4.0f);
            sphere.GetComponent<Fenrir::Transform>().scale = Vec3(0.96f);
            sphere.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{
                store.Add<Fenrir::Mesh>(RendererTest::Sphere(), Fenrir::AssetOwner::App()), material, true});
        }
    }

    test.RunFrame();
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());
    RendererTest::RequireGolden("sphere-grid-gtao", test.ReadTarget(target), FENRIR_TEST_GOLDENS,
                                FENRIR_TEST_GOLDEN_FAILURES);
}
