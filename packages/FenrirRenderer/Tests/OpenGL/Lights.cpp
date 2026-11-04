// Point and spot lights lighting builtin:lit through Fenrir/Lighting.glsl on OpenGL (the spec's seam 2; ADR-0048), read
// back from the view's SceneColor: a white rough wall facing the camera, lit by one light straight in front of the
// pixel at its centre, under a black sky. There the light, the view and the normal are one direction whatever the
// light's distance, so the pixel's colour is the light's falloff times a constant: it falls with the square of the
// distance, and to nothing past the range.

#include <cmath>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirMath/Math.hpp"
#include "GlScene.hpp"

using Fenrir::GlProbe;
using Fenrir::Math::Vec3;
using GlTest::GlScene;

namespace
{
    constexpr std::uint32_t Size = GlScene::Size;
    constexpr std::uint32_t Centre = Size / 2;
    /** @brief How far the wall is in front of the camera. */
    constexpr float WallDistance = 3.0f;

    /** @brief A white rough dielectric wall filling the view, under a black sky, seen by a camera with bloom off. */
    void AddWall(GlScene& test)
    {
        test.AddCamera();
        test.app.GetActiveScene().AddResource<Fenrir::Sky>(Fenrir::ColourSky{{0.0f, 0.0f, 0.0f, 1.0f}});
        const auto material = test.AddQuad(10.0f, WallDistance, "builtin:lit", Fenrir::Colour::White);
        test.assets.Store().SetFloat(material, "roughness", 1.0f);
    }

    /** @brief The pixel column of the wall point `x` units right of the view's centre, with the 60 degree field of
     * view. */
    std::uint32_t ColumnOf(float x)
    {
        const float halfWidth = WallDistance * std::tan(Fenrir::Math::DegToRad(30.0f));
        return static_cast<std::uint32_t>((x / halfWidth + 1.0f) * 0.5f * Size);
    }

    /** @brief A light entity `height` in front of the wall's centre, facing the wall (its -Z is the camera's). */
    Fenrir::Entity AddLightEntity(GlScene& test, float height)
    {
        Fenrir::Entity entity = test.app.GetActiveScene().GetEntityList().CreateEntity();
        entity.GetComponent<Fenrir::Transform>().pos = {0.0f, 0.0f, -WallDistance + height};
        return entity;
    }

    float Red(const GlProbe::Image& image, std::uint32_t x, std::uint32_t y)
    {
        return image.At(x, y)[0];
    }
} // namespace

TEST_CASE("A point light of intensity 1 one unit from a white rough wall lights it about 1", "[renderer][gl][lights]")
{
    GlScene test;
    test.RequireOpenGL();
    AddWall(test);
    AddLightEntity(test, 1.0f)
        .AddComponent<Fenrir::PointLight>(Fenrir::PointLight{Fenrir::Colour::White, 1.0f, 100.0f, false});

    const GlProbe::Image image = test.Frame();
    INFO("the diffuse 1 - F0 = 0.96 and a little specular: as a DirectionalLight of intensity 1 straight on");
    const float lit = Red(image, Centre, Centre);
    REQUIRE(lit > 0.95f);
    REQUIRE(lit < 1.0f);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A point light's light on a wall falls with the square of its distance", "[renderer][gl][lights]")
{
    GlScene test;
    test.RequireOpenGL();
    AddWall(test);
    // A range far past both distances, so its window takes nothing to speak of.
    Fenrir::Entity light = AddLightEntity(test, 0.5f);
    light.AddComponent<Fenrir::PointLight>(Fenrir::PointLight{Fenrir::Colour::White, 1.0f, 100.0f, false});

    const float half = Red(test.Frame(), Centre, Centre);
    light.GetComponent<Fenrir::Transform>().pos.z = -WallDistance + 1.0f;
    const float one = Red(test.Frame(), Centre, Centre);
    light.GetComponent<Fenrir::Transform>().pos.z = -WallDistance + 2.0f;
    const float two = Red(test.Frame(), Centre, Centre);

    INFO("half a unit, one and two: " << half << ", " << one << ", " << two);
    REQUIRE(std::abs(half / one - 4.0f) < 0.04f);
    REQUIRE(std::abs(one / two - 4.0f) < 0.04f);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Nothing past a point light's range is lit", "[renderer][gl][lights]")
{
    GlScene test;
    test.RequireOpenGL();
    AddWall(test);
    // One unit from the wall's centre, reaching 1.5: the centre is lit, the view's corners (2.6 units away) are not.
    Fenrir::Entity light = AddLightEntity(test, 1.0f);
    light.AddComponent<Fenrir::PointLight>(Fenrir::PointLight{Fenrir::Colour::White, 1.0f, 1.5f, false});

    GlProbe::Image image = test.Frame();
    REQUIRE(Red(image, Centre, Centre) > 0.5f);
    REQUIRE(image.At(0, 0) == GlProbe::Pixel{0.0f, 0.0f, 0.0f, 1.0f});
    REQUIRE(image.At(Size - 1, Size - 1) == GlProbe::Pixel{0.0f, 0.0f, 0.0f, 1.0f});

    INFO("two units away with a range of 1.9, it lights nothing at all");
    light.GetComponent<Fenrir::Transform>().pos.z = -WallDistance + 2.0f;
    light.GetComponent<Fenrir::PointLight>().range = 1.9f;
    image = test.Frame();
    REQUIRE(image.At(Centre, Centre) == GlProbe::Pixel{0.0f, 0.0f, 0.0f, 1.0f});
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A spot light lights inside its cone as a point light does, and nothing outside it", "[renderer][gl][lights]")
{
    GlScene test;
    test.RequireOpenGL();
    AddWall(test);
    // One unit from the wall, pointing straight at it, its cone 0.2 to 0.4 radians off its axis.
    AddLightEntity(test, 1.0f)
        .AddComponent<Fenrir::SpotLight>(Fenrir::SpotLight{Fenrir::Colour::White, 1.0f, 100.0f, 0.2f, 0.4f, false});

    const GlProbe::Image image = test.Frame();
    INFO("on its axis, as the point light of intensity 1");
    REQUIRE(Red(image, Centre, Centre) > 0.95f);
    INFO("0.6 radians off its axis, outside the cone, dark, where a point light would light it");
    REQUIRE(Red(image, ColumnOf(std::tan(0.6f)), Centre) == 0.0f);
    REQUIRE(test.messages->errors.empty());
}
