// The sun's cascaded shadow on OpenGL (the spec's seam 2, ADR-0050): a box's shadow on a floor falls where the light's
// direction puts it; a lit floor shows no acne at angles up to grazing within the biases' limits; and posts' shadows
// across every cascade boundary match their golden images, lit and with the cascades tinted.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "GlScene.hpp"

using Fenrir::Math::Vec3;
using GlTest::GlScene;

namespace
{
    /** @brief A lit material of `colour`, rough and dielectric, so the sun lights it alike from every side. */
    Fenrir::AssetHandle<Fenrir::Material> Lit(GlScene& test, Fenrir::Colour colour)
    {
        Fenrir::AssetStore& store = test.assets.Store();
        const auto material = store.CreateMaterial(
            store.Load<Fenrir::Shader>("builtin:lit", Fenrir::AssetOwner::App()).handle, Fenrir::AssetOwner::App());
        store.SetColour(material, "baseColour", colour);
        store.SetFloat(material, "roughness", 1.0f);
        return material;
    }

    /** @brief A box of `size` centred on `centre`. */
    void AddBox(GlScene& test, Vec3 centre, Vec3 size, Fenrir::AssetHandle<Fenrir::Material> material)
    {
        const auto mesh = test.assets.Store().Add<Fenrir::Mesh>(RendererTest::Cube(), Fenrir::AssetOwner::App());
        Fenrir::Entity entity = test.app.GetActiveScene().GetEntityList().CreateEntity();
        entity.GetComponent<Fenrir::Transform>().pos = centre;
        entity.GetComponent<Fenrir::Transform>().scale = size;
        entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{mesh, material, true});
    }

    /** @brief A floor of side `size` at y = -1 facing up, centred under (`x`, `z`). */
    void AddFloor(GlScene& test, float size, float x, float z, Fenrir::AssetHandle<Fenrir::Material> material)
    {
        const auto mesh =
            test.assets.Store().Add<Fenrir::Mesh>(RendererTest::Quad(size, 0.0f), Fenrir::AssetOwner::App());
        Fenrir::Entity entity = test.app.GetActiveScene().GetEntityList().CreateEntity();
        entity.GetComponent<Fenrir::Transform>().pos = {x, -1.0f, z};
        entity.GetComponent<Fenrir::Transform>().rot =
            Fenrir::Math::AngleAxis(Fenrir::Math::DegToRad(-90.0f), Vec3(1.0f, 0.0f, 0.0f));
        entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{mesh, material, true});
    }

    /** @brief The sun, travelling along `direction`, white at intensity 1. */
    Fenrir::DirectionalLight& AddSun(GlScene& test, Vec3 direction)
    {
        Fenrir::Entity sun = test.app.GetActiveScene().GetEntityList().CreateEntity();
        sun.GetComponent<Fenrir::Transform>().rot = glm::quatLookAt(
            glm::normalize(direction),
            std::abs(glm::normalize(direction).y) > 0.99f ? Vec3(0.0f, 0.0f, 1.0f) : Vec3(0.0f, 1.0f, 0.0f));
        return sun.AddComponent<Fenrir::DirectionalLight>();
    }

    /** @brief A render target image of `width` x `height`. */
    Fenrir::AssetHandle<Fenrir::Image> Target(GlScene& test, std::uint32_t width, std::uint32_t height)
    {
        Fenrir::ImageData data;
        data.width = width;
        data.height = height;
        return test.assets.Store().Add<Fenrir::Image>(std::move(data), Fenrir::AssetOwner::App());
    }

    /** @brief A camera entity at `position` turned by `rotation`, drawing into `target`, bloom off. */
    Fenrir::Camera& AddCameraAt(GlScene& test, Vec3 position, Fenrir::Math::Quat rotation,
                                Fenrir::AssetHandle<Fenrir::Image> target)
    {
        Fenrir::Entity entity = test.app.GetActiveScene().GetEntityList().CreateEntity();
        entity.GetComponent<Fenrir::Transform>().pos = position;
        entity.GetComponent<Fenrir::Transform>().rot = rotation;
        Fenrir::Camera& camera = entity.AddComponent<Fenrir::Camera>();
        camera.bloom.intensity = 0.0f;
        camera.target = Fenrir::ImageTarget{target};
        // No tonemap, so a probe reads the lit value itself, clamped.
        camera.tonemapper = Fenrir::Tonemapper::None;
        return camera;
    }

    /** @brief The linear value of an sRGB-encoded byte. */
    float Linear(std::uint8_t encoded)
    {
        const float c = static_cast<float>(encoded) / 255.0f;
        return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }

    /** @brief Pixel (`x`, `y`) of `image`'s green, linear. */
    float GreenAt(const RendererTest::Rgba8Image& image, std::uint32_t x, std::uint32_t y)
    {
        return Linear(image.rgba.at((std::size_t{y} * image.width + x) * 4 + 1));
    }

    // The top-down probe: a camera 6 units above the floor looking straight down, so floor point (x, z) shows at
    // pixel (32 + (x - CameraX) / HalfSpan x 32, 32 + (z - CameraZ) / HalfSpan x 32), screen up being world -Z.
    constexpr std::uint32_t Side = 64;
    constexpr float CameraX = 1.5f;
    constexpr float CameraZ = -6.0f;
    const float HalfSpan = 6.0f * std::tan(1.04719755f * 0.5f);

    /** @brief The pixel the top-down camera shows floor point (`x`, `z`) at. */
    std::array<std::uint32_t, 2> FloorPixel(float x, float z)
    {
        const auto pixel = [](float offset) {
            return static_cast<std::uint32_t>(
                std::lround(static_cast<float>(Side) * 0.5f * (1.0f + offset / HalfSpan) - 0.5f));
        };
        return {pixel(x - CameraX), pixel(z - CameraZ)};
    }
} // namespace

TEST_CASE("A box's shadow on a floor lies where the sun's direction puts it, and nowhere else",
          "[renderer][gl][shadows]")
{
    GlScene test;
    test.RequireOpenGL();
    test.app.GetActiveScene().AddResource<Fenrir::Sky>(Fenrir::ColourSky{Fenrir::Colour{0.02f, 0.02f, 0.02f, 1.0f}});
    const auto white = Lit(test, Fenrir::Colour::White);
    AddFloor(test, 30.0f, 0.0f, -6.0f, white);
    // A unit box from y 0 to 1 over (0, -6); the sun travels +X and down at 45 degrees, so the box's shadow on the
    // floor at y = -1 runs from x 0.5 (its bottom's near edge, moved 1 along) to 2.5 (its top's far edge, moved 2),
    // between z -6.5 and -5.5.
    AddBox(test, {0.0f, 0.5f, -6.0f}, Vec3(1.0f), white);
    AddSun(test, {1.0f, -1.0f, 0.0f});
    const auto target = Target(test, Side, Side);
    AddCameraAt(test, {CameraX, 5.0f, CameraZ},
                Fenrir::Math::AngleAxis(Fenrir::Math::DegToRad(-90.0f), Vec3(1.0f, 0.0f, 0.0f)), target);

    test.RunFrame();
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());
    const RendererTest::Rgba8Image frame = test.ReadTarget(target);

    // White, rough, lit at N.L = cos 45 by a sun of 1 (the BRDF scaled by pi), and a little sky.
    const auto at = [&](float x, float z) {
        const std::array<std::uint32_t, 2> pixel = FloorPixel(x, z);
        return GreenAt(frame, pixel[0], pixel[1]);
    };
    for (const auto& [x, z] :
         {std::array<float, 2>{0.9f, -6.0f}, std::array<float, 2>{1.5f, -6.0f}, std::array<float, 2>{2.2f, -6.0f},
          std::array<float, 2>{1.5f, -6.3f}, std::array<float, 2>{1.5f, -5.7f}})
    {
        CAPTURE(x, z);
        INFO("inside the shadow the floor has only the sky's light: " << at(x, z));
        REQUIRE(at(x, z) < 0.1f);
    }
    for (const auto& [x, z] :
         {std::array<float, 2>{2.9f, -6.0f}, std::array<float, 2>{4.0f, -6.0f}, std::array<float, 2>{1.5f, -7.0f},
          std::array<float, 2>{1.5f, -5.0f}, std::array<float, 2>{1.5f, -8.5f}, std::array<float, 2>{-1.8f, -7.0f}})
    {
        CAPTURE(x, z);
        INFO("outside it the sun lights the floor fully: " << at(x, z));
        REQUIRE(at(x, z) > 0.6f);
    }
}

TEST_CASE("A lit floor shows no shadow acne from the sun at angles up to grazing within the biases' limits",
          "[renderer][gl][shadows]")
{
    // The sun's angle from the floor's normal: overhead to 80 degrees, past which the slope bias stops growing.
    const float angle = GENERATE(0.0f, 30.0f, 60.0f, 75.0f, 80.0f);
    const auto render = [&](bool castShadows) {
        GlScene test;
        test.RequireOpenGL();
        test.app.GetActiveScene().AddResource<Fenrir::Sky>(Fenrir::ColourSky{Fenrir::Colour::Black});
        AddFloor(test, 60.0f, 0.0f, -25.0f, Lit(test, Fenrir::Colour::White));
        const float radians = Fenrir::Math::DegToRad(angle);
        AddSun(test, {std::sin(radians) * 0.6f, -std::cos(radians), -std::sin(radians) * 0.8f}).castShadows =
            castShadows;
        const auto target = Target(test, 96, 64);
        // Above the floor, looking down across it into the distance: every cascade's range is on it.
        AddCameraAt(test, {0.0f, 1.5f, 0.0f}, Fenrir::Math::AngleAxis(-0.35f, Vec3(1.0f, 0.0f, 0.0f)), target);
        test.RunFrame();
        INFO("no GL error or other error was logged");
        REQUIRE(test.messages->errors.empty());
        return test.ReadTarget(target);
    };
    const RendererTest::Rgba8Image shadowed = render(true);
    const RendererTest::Rgba8Image unshadowed = render(false);
    INFO("both frames are the same size");
    REQUIRE(shadowed.width == unshadowed.width);

    // Every floor pixel, from a few rows below the horizon down: lit as it is without shadows.
    float worst = 1.0f;
    std::uint32_t worstX = 0;
    std::uint32_t worstY = 0;
    for (std::uint32_t y = 26; y < shadowed.height; ++y)
    {
        for (std::uint32_t x = 0; x < shadowed.width; ++x)
        {
            const float without = GreenAt(unshadowed, x, y);
            if (without < 0.01f)
            {
                continue;
            }
            const float ratio = GreenAt(shadowed, x, y) / without;
            if (ratio < worst)
            {
                worst = ratio;
                worstX = x;
                worstY = y;
            }
        }
    }
    INFO("with the sun " << angle << " degrees from the floor's normal, the darkest floor pixel against the unshadowed "
                         << "frame is at (" << worstX << ", " << worstY << "): " << worst);
    REQUIRE(worst > 0.97f);
}

TEST_CASE("Posts' shadows across every cascade boundary match their golden images, lit and tinted by cascade",
          "[renderer][gl][golden][shadows]")
{
    const bool tinted = GENERATE(false, true);
    GlScene test;
    test.RequireOpenGL();
    test.app.GetActiveScene().AddResource<Fenrir::Sky>(Fenrir::ColourSky{Fenrir::Colour{0.5f, 0.6f, 0.75f, 1.0f}});
    const auto floorMaterial = Lit(test, Fenrir::Colour{0.7f, 0.7f, 0.7f, 1.0f});
    AddFloor(test, 80.0f, 0.0f, -30.0f, floorMaterial);
    // Two rows of thin posts receding from the camera, every two units out to 40, under a low sun from the left: their
    // shadows lie across the floor in every cascade, and across each boundary between two.
    const auto post = Lit(test, Fenrir::Colour{0.8f, 0.25f, 0.15f, 1.0f});
    for (int i = 1; i <= 20; ++i)
    {
        const float z = -2.0f * static_cast<float>(i);
        AddBox(test, {-1.5f, 0.0f, z}, {0.15f, 2.0f, 0.15f}, post);
        AddBox(test, {1.8f, -0.25f, z - 1.0f}, {0.3f, 1.5f, 0.3f}, post);
    }
    Fenrir::DirectionalLight& sun = AddSun(test, {0.8f, -0.45f, -0.25f});
    sun.intensity = 2.0f;
    // Near, so the four cascades and their boundaries all fall within the frame.
    sun.shadowDistance = 40.0f;
    const auto target = Target(test, 160, 90);
    Fenrir::Camera& camera =
        AddCameraAt(test, {0.0f, 1.2f, 0.5f}, Fenrir::Math::AngleAxis(-0.18f, Vec3(1.0f, 0.0f, 0.0f)), target);
    camera.tonemapper = Fenrir::Tonemapper::AgX;
    if (tinted)
    {
        camera.debugView = Fenrir::CameraDebugView::ShadowCascades;
    }

    test.RunFrame();
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());
    RendererTest::RequireGolden(tinted ? "shadow-cascades-tinted" : "shadow-cascades", test.ReadTarget(target),
                                FENRIR_TEST_GOLDENS, FENRIR_TEST_GOLDEN_FAILURES);
}

TEST_CASE("A camera showing its shadow atlas draws the atlas as grey, and black where nothing casts",
          "[renderer][gl][shadows]")
{
    const bool casters = GENERATE(true, false);
    GlScene test;
    test.RequireOpenGL();
    const auto white = Lit(test, Fenrir::Colour::White);
    AddFloor(test, 30.0f, 0.0f, -6.0f, white);
    AddBox(test, {0.0f, 0.5f, -6.0f}, Vec3(1.0f), white);
    AddSun(test, {0.0f, -1.0f, -0.2f}).castShadows = casters;
    const auto target = Target(test, 64, 16);
    Fenrir::Camera& camera = AddCameraAt(test, {0.0f, 0.5f, 0.0f}, Fenrir::Math::Quat(1.0f, 0.0f, 0.0f, 0.0f), target);
    camera.debugView = Fenrir::CameraDebugView::ShadowAtlas;

    test.RunFrame();
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());
    const RendererTest::Rgba8Image frame = test.ReadTarget(target);
    float brightest = 0.0f;
    for (std::uint32_t y = 0; y < frame.height; ++y)
    {
        for (std::uint32_t x = 0; x < frame.width; ++x)
        {
            brightest = std::max(brightest, GreenAt(frame, x, y));
        }
    }
    INFO("the brightest pixel is " << brightest);
    if (casters)
    {
        INFO("the floor and the box lie in the atlas, nearer the sun than its far plane");
        REQUIRE(brightest > 0.05f);
    }
    else
    {
        INFO("with nothing casting there is no atlas, and the view is black");
        REQUIRE(brightest == 0.0f);
    }
}
