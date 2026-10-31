// Rect lights lighting builtin:lit through Fenrir/Lighting.glsl's linearly transformed cosines on OpenGL (the spec's
// seam 2; ADR-0048), read back from the view's SceneColor: a white rough wall facing the camera under a black sky, lit
// by one rect light between them. Then a polished floor of three roughnesses under a panel, a neon strip and a
// two-sided sign, against its golden image: each light's reflection takes its shape and blurs with the roughness.

#include <cmath>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirMath/Math.hpp"
#include "GlScene.hpp"

using Fenrir::GlProbe;
using Fenrir::Math::Quat;
using Fenrir::Math::Vec3;
using GlTest::GlScene;

namespace
{
    constexpr std::uint32_t Size = GlScene::Size;
    constexpr std::uint32_t Centre = Size / 2;
    /** @brief How far the wall is in front of the camera. */
    constexpr float WallDistance = 3.0f;
    constexpr float HalfTurn = 3.14159265f;

    /** @brief A white rough dielectric wall filling the view, under a black sky, seen by a camera with bloom off. */
    void AddWall(GlScene& test)
    {
        test.AddCamera();
        test.app.GetActiveScene().AddResource<Fenrir::Sky>(Fenrir::ColourSky{{0.0f, 0.0f, 0.0f, 1.0f}});
        const auto material = test.AddQuad(10.0f, WallDistance, "builtin:lit", Fenrir::Colour::White);
        test.assets.Store().SetFloat(material, "roughness", 1.0f);
    }

    /** @brief A rect light `height` in front of the wall's centre, facing it (its -Z is the camera's). */
    Fenrir::Entity AddRectLight(GlScene& test, float height, Fenrir::RectLight light)
    {
        Fenrir::Entity entity = test.app.GetActiveScene().GetEntityList().CreateEntity();
        entity.GetComponent<Fenrir::Transform>().pos = {0.0f, 0.0f, -WallDistance + height};
        entity.AddComponent<Fenrir::RectLight>(light);
        return entity;
    }

    float Red(const GlProbe::Image& image, std::uint32_t x, std::uint32_t y)
    {
        return image.At(x, y)[0];
    }
} // namespace

TEST_CASE("A white rect light of intensity 1 filling a white rough wall's view lights it about 1",
          "[renderer][gl][lights][rect]")
{
    GlScene test;
    test.RequireOpenGL();
    AddWall(test);
    // Half a unit in front and a hundred wide: the wall's whole hemisphere sees it.
    AddRectLight(test, 0.5f, Fenrir::RectLight{Fenrir::Colour::White, 1.0f, 100.0f, 100.0f, false, 1000.0f});

    const GlProbe::Image image = test.Frame();
    INFO("its diffuse 1 less what the specular lobe takes, which the specular gives back: as a white sky all round");
    const float lit = Red(image, Centre, Centre);
    INFO("the wall's centre reads " << lit);
    REQUIRE(lit > 0.95f);
    REQUIRE(lit < 1.05f);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A one-sided rect light lights nothing behind it; a two-sided one lights it as its front does",
          "[renderer][gl][lights][rect]")
{
    GlScene test;
    test.RequireOpenGL();
    AddWall(test);
    // One unit in front of the wall, a unit square, facing it.
    Fenrir::Entity light =
        AddRectLight(test, 1.0f, Fenrir::RectLight{Fenrir::Colour::White, 4.0f, 1.0f, 1.0f, false, 100.0f});
    const float front = Red(test.Frame(), Centre, Centre);
    INFO("facing the wall, it lights its centre: " << front);
    REQUIRE(front > 0.2f);

    INFO("turned to face the camera, the wall is behind it and black everywhere");
    light.GetComponent<Fenrir::Transform>().rot = Fenrir::Math::AngleAxis(HalfTurn, Vec3(0.0f, 1.0f, 0.0f));
    const GlProbe::Image behind = test.Frame();
    REQUIRE(behind.At(Centre, Centre) == GlProbe::Pixel{0.0f, 0.0f, 0.0f, 1.0f});
    REQUIRE(behind.At(Centre / 2, Centre / 2) == GlProbe::Pixel{0.0f, 0.0f, 0.0f, 1.0f});

    INFO("two-sided, its back lights the wall as its front did");
    light.GetComponent<Fenrir::RectLight>().twoSided = true;
    const float back = Red(test.Frame(), Centre, Centre);
    INFO("front " << front << ", back " << back);
    REQUIRE(std::abs(back - front) < 0.01f * front);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Nothing past a rect light's range from the rectangle is lit", "[renderer][gl][lights][rect]")
{
    GlScene test;
    test.RequireOpenGL();
    AddWall(test);
    // One unit from the wall, reaching 1.5: the wall's centre is lit, the view's corners are not.
    AddRectLight(test, 1.0f, Fenrir::RectLight{Fenrir::Colour::White, 4.0f, 1.0f, 1.0f, false, 1.5f});
    const GlProbe::Image image = test.Frame();
    REQUIRE(Red(image, Centre, Centre) > 0.1f);
    REQUIRE(image.At(0, 0) == GlProbe::Pixel{0.0f, 0.0f, 0.0f, 1.0f});
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A polished floor of three roughnesses reflecting a panel, a neon strip and a two-sided sign matches its "
          "golden image",
          "[renderer][gl][golden][lights][rect]")
{
    GlScene test;
    test.RequireOpenGL();
    Fenrir::AssetStore& store = test.assets.Store();
    Fenrir::EntityList& entities = test.app.GetActiveScene().GetEntityList();
    Fenrir::ImageData data;
    data.width = 160;
    data.height = 96;
    const auto target = store.Add<Fenrir::Image>(std::move(data), Fenrir::AssetOwner::App());
    // A metre and a half up, looking down the room a little.
    Fenrir::Entity camera = entities.CreateEntity();
    camera.GetComponent<Fenrir::Transform>().pos = {0.0f, 1.5f, 0.0f};
    camera.GetComponent<Fenrir::Transform>().rot = Fenrir::Math::AngleAxis(-0.25f, Vec3(1.0f, 0.0f, 0.0f));
    Fenrir::Camera& view = camera.AddComponent<Fenrir::Camera>();
    view.target = Fenrir::ImageTarget{target};
    view.bloom.intensity = 0.0f;
    test.app.GetActiveScene().AddResource<Fenrir::Sky>(Fenrir::ColourSky{{0.0f, 0.0f, 0.0f, 1.0f}});

    const auto quad = store.Add<Fenrir::Mesh>(RendererTest::Quad(1.0f), Fenrir::AssetOwner::App());
    const auto lit = store.Load<Fenrir::Shader>("builtin:lit", Fenrir::AssetOwner::App()).handle;
    const auto unlit = store.Load<Fenrir::Shader>("builtin:unlit", Fenrir::AssetOwner::App()).handle;

    // The floor: three dark strips down the room, from a near mirror on the left to rough on the right.
    const Quat flat = Fenrir::Math::AngleAxis(-HalfTurn * 0.5f, Vec3(1.0f, 0.0f, 0.0f));
    for (int strip = 0; strip < 3; ++strip)
    {
        const auto material = store.CreateMaterial(lit, Fenrir::AssetOwner::App());
        store.SetColour(material, "baseColour", {0.05f, 0.05f, 0.05f, 1.0f});
        store.SetFloat(material, "roughness", 0.05f + 0.2f * static_cast<float>(strip));
        Fenrir::Entity floor = entities.CreateEntity();
        Fenrir::Transform& transform = floor.GetComponent<Fenrir::Transform>();
        transform.pos = {-2.0f + 2.0f * static_cast<float>(strip), 0.0f, -6.0f};
        transform.rot = flat;
        transform.scale = {2.0f, 12.0f, 1.0f};
        floor.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{quad, material});
    }

    // Each light, and a quad of its radiance where it is, facing the way it shines (its back too when two-sided).
    const auto addLight = [&](Vec3 position, Quat rotation, Fenrir::RectLight light) {
        Fenrir::Entity entity = entities.CreateEntity();
        entity.GetComponent<Fenrir::Transform>().pos = position;
        entity.GetComponent<Fenrir::Transform>().rot = rotation;
        entity.AddComponent<Fenrir::RectLight>(light);
        const auto glow = store.CreateMaterial(unlit, Fenrir::AssetOwner::App());
        store.SetColour(glow, "baseColour",
                        {light.colour.r * light.intensity, light.colour.g * light.intensity,
                         light.colour.b * light.intensity, 1.0f});
        store.SetDoubleSided(glow, light.twoSided);
        Fenrir::Entity shape = entities.CreateEntity();
        Fenrir::Transform& transform = shape.GetComponent<Fenrir::Transform>();
        transform.pos = position;
        transform.rot = rotation * Fenrir::Math::AngleAxis(HalfTurn, Vec3(0.0f, 1.0f, 0.0f));
        transform.scale = {light.width, light.height, 1.0f};
        shape.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{quad, glow});
    };
    const Quat towardsCamera = Fenrir::Math::AngleAxis(HalfTurn, Vec3(0.0f, 1.0f, 0.0f));
    // A warm white panel low across the back of the room and a thin magenta neon strip just above the floor across the
    // middle, each as wide as the floor, so each one's reflection crosses all three strips.
    addLight({0.0f, 0.65f, -9.0f}, towardsCamera, {{1.0f, 0.9f, 0.75f, 1.0f}, 3.0f, 6.0f, 1.0f, false, 30.0f});
    addLight({0.0f, 0.15f, -5.0f}, towardsCamera, {{1.0f, 0.1f, 0.6f, 1.0f}, 6.0f, 5.0f, 0.06f, false, 20.0f});
    // A cyan two-sided sign standing side on at the right, lighting both ways across the room.
    addLight({2.6f, 0.8f, -6.5f}, Fenrir::Math::AngleAxis(HalfTurn * 0.5f, Vec3(0.0f, 1.0f, 0.0f)),
             {{0.1f, 0.8f, 1.0f, 1.0f}, 4.0f, 1.2f, 0.8f, true, 20.0f});

    test.RunFrame();
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());
    RendererTest::RequireGolden("showroom-floor", test.ReadTarget(target), FENRIR_TEST_GOLDENS,
                                FENRIR_TEST_GOLDEN_FAILURES);
}
