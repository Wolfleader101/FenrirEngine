// The scene's Sky lighting builtin:lit through Fenrir/Lighting.glsl on OpenGL (the spec's seam 2; ADR-0047, ADR-0048):
// a white rough sphere, dielectric or metal, under a constant white sky reflects about 1 everywhere, a mirror sphere
// reflects the sky's faces, an occlusion texture darkens the sky's light but not the sun's, read back from the view's
// SceneColor, and a golden image of a grid of spheres under one panorama (compared as in Tonemapping.cpp;
// FENRIR_UPDATE_GOLDENS=1 writes it again).

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

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

    /**
     * @brief A sphere of radius `radius` at `centre` in builtin:lit, of `colour`, `metallic` and `roughness`, and its
     * material.
     */
    Fenrir::AssetHandle<Fenrir::Material> AddSphere(GlScene& test, Vec3 centre, float radius, Fenrir::Colour colour,
                                                    float metallic, float roughness)
    {
        Fenrir::AssetStore& store = test.assets.Store();
        const auto material = store.CreateMaterial(
            store.Load<Fenrir::Shader>("builtin:lit", Fenrir::AssetOwner::App()).handle, Fenrir::AssetOwner::App());
        store.SetColour(material, "baseColour", colour);
        store.SetFloat(material, "metallic", metallic);
        store.SetFloat(material, "roughness", roughness);
        Fenrir::Entity sphere = test.app.GetActiveScene().GetEntityList().CreateEntity();
        sphere.GetComponent<Fenrir::Transform>().pos = centre;
        sphere.GetComponent<Fenrir::Transform>().scale = Vec3(radius * 2.0f);
        sphere.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{
            store.Add<Fenrir::Mesh>(RendererTest::Sphere(), Fenrir::AssetOwner::App()), material, true});
        return material;
    }

    /**
     * @brief The pixel of the main window's view, from the camera at the origin looking along -Z with the default 60
     * degree field of view, that the world point `point` falls on.
     */
    std::pair<std::uint32_t, std::uint32_t> PixelOf(Vec3 point)
    {
        const float focal = 1.0f / std::tan(Fenrir::Math::DegToRad(30.0f));
        const float x = focal * point.x / -point.z;
        const float y = focal * point.y / -point.z;
        return {static_cast<std::uint32_t>((x + 1.0f) * 0.5f * Size),
                static_cast<std::uint32_t>((1.0f - y) * 0.5f * Size)};
    }

    using Rgb = std::array<std::uint8_t, 3>;

    // One colour per face, in face order (+X, -X, +Y, -Y, +Z, -Z), each channel 0 or 255, so sRGB and linear agree.
    constexpr std::array<Rgb, 6> FaceColours{{
        {255, 0, 0},
        {0, 255, 255},
        {0, 255, 0},
        {255, 0, 255},
        {0, 0, 255},
        {255, 255, 0},
    }};

    /** @brief A cubemap of 16 x 16 faces, each FaceColours' colour. */
    Fenrir::ImageData FacesCubemap()
    {
        constexpr std::uint32_t side = 16;
        Fenrir::ImageData image;
        image.width = side;
        image.height = side;
        image.shape = Fenrir::ImageShape::Cubemap;
        for (const Rgb& rgb : FaceColours)
        {
            for (std::uint32_t texel = 0; texel < side * side; ++texel)
            {
                for (const std::uint8_t channel : {rgb[0], rgb[1], rgb[2], std::uint8_t{255}})
                {
                    image.pixels.push_back(std::byte{channel});
                }
            }
        }
        return image;
    }

    GlProbe::Pixel ColourOf(const Rgb& rgb)
    {
        return {rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, 1.0f};
    }
} // namespace

TEST_CASE("A white rough sphere under a constant white panorama sky reflects about 1 all over",
          "[renderer][gl][sky][lighting]")
{
    GlScene test;
    test.RequireOpenGL();
    test.AddCamera();
    const auto panorama = test.assets.Store().Add<Fenrir::Image>(
        RendererTest::Panorama(64, [](Vec3) { return Vec3(1.0f); }), Fenrir::AssetOwner::App());
    test.app.GetActiveScene().AddResource<Fenrir::Sky>(Fenrir::PanoramaSky{panorama});
    AddSphere(test, Vec3(0.0f, 0.0f, -3.0f), 1.0f, Fenrir::Colour::White, 0.0f, 1.0f);

    const GlProbe::Image image = test.Frame();

    // Irradiance pi all round, so the diffuse reflects 1 less what the specular takes, and the specular gives that back
    // from the white prefiltered cube: about 1 whatever the angle. No directional light.
    for (const Vec3 normal : {Vec3(0.0f, 0.0f, 1.0f), glm::normalize(Vec3(0.6f, 0.0f, 1.0f)),
                              glm::normalize(Vec3(0.0f, 0.7f, 1.0f)), glm::normalize(Vec3(-0.5f, -0.5f, 1.0f))})
    {
        const auto [x, y] = PixelOf(Vec3(0.0f, 0.0f, -3.0f) + normal);
        INFO("the sphere where its normal is (" << normal.x << ", " << normal.y << ", " << normal.z << "), pixel (" << x
                                                << ", " << y << "), reads about 1");
        REQUIRE(GlProbe::Near(image.At(x, y), {1.0f, 1.0f, 1.0f, 1.0f}, 0.05f));
    }
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A white rough sphere under a white ColourSky reflects about 1", "[renderer][gl][sky][lighting]")
{
    GlScene test;
    test.RequireOpenGL();
    test.AddCamera();
    test.app.GetActiveScene().AddResource<Fenrir::Sky>(Fenrir::ColourSky{Fenrir::Colour::White});
    AddSphere(test, Vec3(0.0f, 0.0f, -3.0f), 1.0f, Fenrir::Colour::White, 0.0f, 1.0f);

    const GlProbe::Image image = test.Frame();

    INFO("the sphere's centre reads about 1, as under the white panorama");
    REQUIRE(GlProbe::Near(image.At(Centre, Centre), {1.0f, 1.0f, 1.0f, 1.0f}, 0.05f));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A mirror sphere reflects the sky's faces", "[renderer][gl][sky][lighting][cubemap]")
{
    GlScene test;
    test.RequireOpenGL();
    test.AddCamera();
    const auto cubemap = test.assets.Store().Add<Fenrir::Image>(FacesCubemap(), Fenrir::AssetOwner::App());
    test.app.GetActiveScene().AddResource<Fenrir::Sky>(Fenrir::CubemapSky{cubemap});
    const Vec3 centre(0.0f, 0.0f, -3.0f);
    AddSphere(test, centre, 1.0f, Fenrir::Colour::White, 1.0f, 0.0f);

    const GlProbe::Image image = test.Frame();

    INFO("its centre faces the camera and reflects what is behind it: the +Z face, blue");
    REQUIRE(GlProbe::Near(image.At(Centre, Centre), ColourOf(FaceColours[4]), 0.08f));
    // Where the normal leans 45 degrees up, the view reflects to (0, 0.95, -0.3): the +Y face; turned 45 degrees right,
    // to (0.95, 0, -0.3): the +X face.
    const auto [upX, upY] = PixelOf(centre + glm::normalize(Vec3(0.0f, 1.0f, 1.0f)));
    INFO("where its normal leans up 45 degrees it reflects the +Y face, green");
    REQUIRE(GlProbe::Near(image.At(upX, upY), ColourOf(FaceColours[2]), 0.08f));
    const auto [rightX, rightY] = PixelOf(centre + glm::normalize(Vec3(1.0f, 0.0f, 1.0f)));
    INFO("where its normal leans right 45 degrees it reflects the +X face, red");
    REQUIRE(GlProbe::Near(image.At(rightX, rightY), ColourOf(FaceColours[0]), 0.08f));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A white SkyLight lights a white rough sphere about 1 while a coloured skybox is drawn behind it",
          "[renderer][gl][sky][lighting][cubemap]")
{
    GlScene test;
    test.RequireOpenGL();
    test.AddCamera();
    const auto cubemap = test.assets.Store().Add<Fenrir::Image>(FacesCubemap(), Fenrir::AssetOwner::App());
    test.app.GetActiveScene().AddResource<Fenrir::Sky>(Fenrir::CubemapSky{cubemap});
    test.app.GetActiveScene().AddResource<Fenrir::SkyLight>(Fenrir::SkyLight{Fenrir::ColourSky{Fenrir::Colour::White}});
    AddSphere(test, Vec3(0.0f, 0.0f, -3.0f), 1.0f, Fenrir::Colour::White, 0.0f, 1.0f);

    const GlProbe::Image image = test.Frame();

    INFO("the sphere's centre reads about 1, white, not the faces' colours");
    REQUIRE(GlProbe::Near(image.At(Centre, Centre), {1.0f, 1.0f, 1.0f, 1.0f}, 0.05f));
    INFO("a corner, looking mostly along -Z, still shows the skybox's -Z face, yellow");
    REQUIRE(GlProbe::Near(image.At(0, 0), ColourOf(FaceColours[5]), 0.02f));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A white rough metal sphere under a white sky reflects about 1, losing nothing to its microfacets",
          "[renderer][gl][sky][lighting][furnace]")
{
    GlScene test;
    test.RequireOpenGL();
    test.AddCamera();
    test.app.GetActiveScene().AddResource<Fenrir::Sky>(Fenrir::ColourSky{Fenrir::Colour::White});
    const Vec3 centre(0.0f, 0.0f, -3.0f);
    AddSphere(test, centre, 1.0f, Fenrir::Colour::White, 1.0f, 1.0f);

    const GlProbe::Image image = test.Frame();

    // Single scattering alone reflects about 0.6 of the sky at roughness 1; multiple scattering gives back the rest.
    for (const Vec3 normal : {Vec3(0.0f, 0.0f, 1.0f), glm::normalize(Vec3(0.7f, 0.0f, 1.0f))})
    {
        const auto [x, y] = PixelOf(centre + normal);
        INFO("the sphere where its normal is (" << normal.x << ", " << normal.y << ", " << normal.z
                                                << ") reads about 1");
        REQUIRE(GlProbe::Near(image.At(x, y), {1.0f, 1.0f, 1.0f, 1.0f}, 0.05f));
    }
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("An occlusion texture darkens a sphere lit by the sky to its own value", "[renderer][gl][sky][lighting]")
{
    GlScene test;
    test.RequireOpenGL();
    test.AddCamera();
    test.app.GetActiveScene().AddResource<Fenrir::Sky>(Fenrir::ColourSky{Fenrir::Colour::White});
    const Vec3 left(-1.2f, 0.0f, -4.0f);
    const Vec3 right(1.2f, 0.0f, -4.0f);
    const auto occluded = AddSphere(test, left, 1.0f, Fenrir::Colour::White, 0.0f, 1.0f);
    AddSphere(test, right, 1.0f, Fenrir::Colour::White, 0.0f, 1.0f);
    // Red 64 is a quarter; the other channels, 255, would read 1 if the wrong one were used.
    INFO("the occlusion texture is set");
    REQUIRE(test.assets.Store().SetTexture(
        occluded, "occlusionTexture",
        test.assets.Store().Add<Fenrir::Image>(
            RendererTest::Picture(1, 1, {64, 255, 255, 255}, Fenrir::ColourSpace::Linear), Fenrir::AssetOwner::App())));

    const GlProbe::Image image = test.Frame();

    const auto [leftX, leftY] = PixelOf(left + Vec3(0.0f, 0.0f, 1.0f));
    const auto [rightX, rightY] = PixelOf(right + Vec3(0.0f, 0.0f, 1.0f));
    INFO("the sphere without an occlusion texture reads about 1, as white under a white sky");
    REQUIRE(GlProbe::Near(image.At(rightX, rightY), {1.0f, 1.0f, 1.0f, 1.0f}, 0.05f));
    INFO("the one whose occlusion texture's red is a quarter reads about a quarter");
    REQUIRE(GlProbe::Near(image.At(leftX, leftY), {0.25f, 0.25f, 0.25f, 1.0f}, 0.03f));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("An occlusion texture leaves a sphere lit by the sun alone as it is", "[renderer][gl][sky][lighting]")
{
    GlScene test;
    test.RequireOpenGL();
    test.AddCamera();
    test.app.GetActiveScene().AddResource<Fenrir::Sky>(Fenrir::ColourSky{Fenrir::Colour::Black});
    // Shining along -Z, from behind the camera.
    Fenrir::Entity sun = test.app.GetActiveScene().GetEntityList().CreateEntity();
    sun.AddComponent<Fenrir::DirectionalLight>(Fenrir::DirectionalLight{Fenrir::Colour::White, 1.0f});
    const Vec3 left(-1.2f, 0.0f, -4.0f);
    const Vec3 right(1.2f, 0.0f, -4.0f);
    const auto occluded = AddSphere(test, left, 1.0f, Fenrir::Colour::White, 0.0f, 0.5f);
    AddSphere(test, right, 1.0f, Fenrir::Colour::White, 0.0f, 0.5f);
    INFO("the occlusion texture is set");
    REQUIRE(test.assets.Store().SetTexture(
        occluded, "occlusionTexture",
        test.assets.Store().Add<Fenrir::Image>(
            RendererTest::Picture(1, 1, {64, 64, 64, 255}, Fenrir::ColourSpace::Linear), Fenrir::AssetOwner::App())));

    const GlProbe::Image image = test.Frame();

    // The two points mirror each other across the view's centre, so they are lit and seen alike.
    const auto [leftX, leftY] = PixelOf(left + Vec3(0.0f, 0.0f, 1.0f));
    const auto [rightX, rightY] = PixelOf(right + Vec3(0.0f, 0.0f, 1.0f));
    const GlProbe::Pixel unoccluded = image.At(rightX, rightY);
    INFO("the sun lights the sphere without an occlusion texture: its red is " << unoccluded[0]);
    REQUIRE(unoccluded[0] > 0.5f);
    INFO("and the one with a dark occlusion texture just the same");
    REQUIRE(GlProbe::Near(image.At(leftX, leftY), unoccluded, 0.01f));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Rows of dielectric and metal spheres from smooth to rough under a panorama match their golden image",
          "[renderer][gl][golden][lighting]")
{
    GlScene test;
    test.RequireOpenGL();
    Fenrir::AssetStore& store = test.assets.Store();
    Fenrir::ImageData data;
    data.width = 160;
    data.height = 72;
    const auto target = store.Add<Fenrir::Image>(std::move(data), Fenrir::AssetOwner::App());
    test.AddCamera().target = Fenrir::ImageTarget{target};

    // A blue sky over a warm horizon and brown ground, with a bright sun up, to the right and behind the camera, so the
    // smooth spheres show its highlight and the rough ones a broad glow.
    const Vec3 sun = glm::normalize(Vec3(0.6f, 0.5f, 0.6f));
    const auto sky = [&](Vec3 direction) {
        if (glm::dot(direction, sun) > std::cos(Fenrir::Math::DegToRad(4.0f)))
        {
            return Vec3(40.0f, 36.0f, 30.0f);
        }
        if (direction.y < 0.0f)
        {
            return Vec3(0.25f, 0.18f, 0.12f) * (1.0f + 0.5f * direction.y);
        }
        return glm::mix(Vec3(1.5f, 1.3f, 1.0f), Vec3(0.15f, 0.35f, 0.9f), std::sqrt(direction.y));
    };
    const auto panorama = store.Add<Fenrir::Image>(RendererTest::Panorama(256, sky), Fenrir::AssetOwner::App());
    test.app.GetActiveScene().AddResource<Fenrir::Sky>(Fenrir::PanoramaSky{panorama});

    // The top row a red dielectric, the bottom row gold, each from roughness 0 on the left to 1 on the right.
    for (int column = 0; column < 5; ++column)
    {
        const float roughness = static_cast<float>(column) / 4.0f;
        const float x = -2.2f + 1.1f * static_cast<float>(column);
        AddSphere(test, Vec3(x, 0.55f, -4.0f), 0.48f, Fenrir::Colour{0.8f, 0.1f, 0.08f, 1.0f}, 0.0f, roughness);
        AddSphere(test, Vec3(x, -0.55f, -4.0f), 0.48f, Fenrir::Colour{1.0f, 0.78f, 0.34f, 1.0f}, 1.0f, roughness);
    }

    test.RunFrame();
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());
    RendererTest::RequireGolden("sphere-grid-panorama", test.ReadTarget(target), FENRIR_TEST_GOLDENS,
                                FENRIR_TEST_GOLDEN_FAILURES);
}
