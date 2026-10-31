// The scene's Sky on OpenGL (the spec's seam 2): the Background pass's colour, cubemap or panorama in the pixels
// nothing drew, read back from the view's SceneColor, and a golden image of a panorama sky behind a lit sphere
// (compared as in Tonemapping.cpp; FENRIR_UPDATE_GOLDENS=1 writes it again).

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <utility>
#include <vector>

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

    GlProbe::Pixel PixelOf(const Rgb& rgb)
    {
        return {rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, 1.0f};
    }

    /**
     * @brief A cubemap of 4 x 4 faces, each FaceColours' colour, except the -Z face's bottom two rows, which are
     * `bottom`.
     */
    Fenrir::ImageData Cubemap(const Rgb& bottom)
    {
        constexpr std::uint32_t side = 4;
        Fenrir::ImageData image;
        image.width = side;
        image.height = side;
        image.shape = Fenrir::ImageShape::Cubemap;
        for (std::size_t face = 0; face < FaceColours.size(); ++face)
        {
            for (std::uint32_t row = 0; row < side; ++row)
            {
                const Rgb& rgb = face == 5 && row >= side / 2 ? bottom : FaceColours.at(face);
                for (std::uint32_t column = 0; column < side; ++column)
                {
                    for (const std::uint8_t channel : {rgb[0], rgb[1], rgb[2], std::uint8_t{255}})
                    {
                        image.pixels.push_back(std::byte{channel});
                    }
                }
            }
        }
        return image;
    }

    /** @brief A camera at the origin, turned by `yaw` about +Y and then `pitch` about +X, in degrees. */
    Fenrir::Camera& AddTurnedCamera(GlScene& test, float yaw, float pitch)
    {
        Fenrir::Entity entity = test.app.GetActiveScene().GetEntityList().CreateEntity();
        entity.GetComponent<Fenrir::Transform>().rot =
            glm::angleAxis(Fenrir::Math::DegToRad(yaw), Fenrir::Math::Vec3(0.0f, 1.0f, 0.0f)) *
            glm::angleAxis(Fenrir::Math::DegToRad(pitch), Fenrir::Math::Vec3(1.0f, 0.0f, 0.0f));
        return entity.AddComponent<Fenrir::Camera>();
    }

    Fenrir::Sky& AddCubemapSky(GlScene& test, const Rgb& bottom = FaceColours[5])
    {
        const auto cubemap = test.assets.Store().Add<Fenrir::Image>(Cubemap(bottom), Fenrir::AssetOwner::App());
        return test.app.GetActiveScene().AddResource<Fenrir::Sky>(Fenrir::CubemapSky{cubemap});
    }

    /**
     * @brief The world direction a panorama's point (u, v) looks along, both in [0, 1]: its centre looks along -Z, u
     * turns towards +X (to the right, seen from inside), and its top row looks up +Y.
     */
    Vec3 PanoramaDirection(float u, float v)
    {
        const float azimuth = (u - 0.5f) * 2.0f * std::numbers::pi_v<float>;
        const float polar = v * std::numbers::pi_v<float>;
        return {std::sin(polar) * std::sin(azimuth), std::cos(polar), -std::sin(polar) * std::cos(azimuth)};
    }

    /** @brief A flat RGBA32F panorama of `width` x `width / 2`, linear, each pixel `colour`'s of its direction. */
    template <typename ColourOf>
    Fenrir::ImageData Panorama(std::uint32_t width, ColourOf colour)
    {
        Fenrir::ImageData image;
        image.width = width;
        image.height = width / 2;
        image.format = Fenrir::ImageFormat::RGBA32F;
        image.colourSpace = Fenrir::ColourSpace::Linear;
        std::vector<float> texels;
        for (std::uint32_t row = 0; row < image.height; ++row)
        {
            for (std::uint32_t column = 0; column < image.width; ++column)
            {
                const Vec3 rgb = colour(PanoramaDirection((static_cast<float>(column) + 0.5f) / image.width,
                                                          (static_cast<float>(row) + 0.5f) / image.height));
                texels.insert(texels.end(), {rgb.r, rgb.g, rgb.b, 1.0f});
            }
        }
        const auto bytes = std::as_bytes(std::span(texels));
        image.pixels.assign(bytes.begin(), bytes.end());
        return image;
    }

    /** @brief FaceColours' colour of the cube face `direction` points at: its largest component's axis and sign. */
    Vec3 FaceColourOf(Vec3 direction)
    {
        const Vec3 size = glm::abs(direction);
        const int axis = size.x >= size.y && size.x >= size.z ? 0 : (size.y >= size.z ? 1 : 2);
        const std::size_t face = static_cast<std::size_t>(axis) * 2 + (direction[axis] < 0.0f ? 1 : 0);
        const Rgb& rgb = FaceColours.at(face);
        return Vec3(rgb[0], rgb[1], rgb[2]) / 255.0f;
    }

    Fenrir::Sky& AddPanoramaSky(GlScene& test, float intensity = 1.0f, float rotation = 0.0f)
    {
        const auto panorama =
            test.assets.Store().Add<Fenrir::Image>(Panorama(64, FaceColourOf), Fenrir::AssetOwner::App());
        return test.app.GetActiveScene().AddResource<Fenrir::Sky>(Fenrir::PanoramaSky{panorama, intensity, rotation});
    }

    // Looking along -Z, then turned to look along +X, -X, +Y and -Y, with the face each look sees.
    const std::array<std::pair<std::array<float, 2>, std::size_t>, 5> Looks{{
        {{0.0f, 0.0f}, 5},
        {{-90.0f, 0.0f}, 0},
        {{90.0f, 0.0f}, 1},
        {{0.0f, 90.0f}, 2},
        {{0.0f, -90.0f}, 3},
    }};
} // namespace

TEST_CASE("A ColourSky fills the pixels nothing drew, and an opaque quad covers it", "[renderer][gl][sky]")
{
    GlScene test;
    test.RequireOpenGL();
    test.AddCamera();
    test.app.GetActiveScene().AddResource<Fenrir::Sky>(Fenrir::ColourSky{Fenrir::Colour{0.1f, 0.3f, 0.6f, 1.0f}});
    test.AddQuad(1.0f, 2.0f, "builtin:unlit", Fenrir::Colour::Red);

    const GlProbe::Image image = test.Frame();

    INFO("a corner, which nothing covers, reads the sky's colour");
    REQUIRE(GlProbe::Near(image.At(0, 0), {0.1f, 0.3f, 0.6f, 1.0f}));
    INFO("the centre, under the quad, reads the quad, not the sky");
    REQUIRE(GlProbe::Near(image.At(Centre, Centre), {1.0f, 0.0f, 0.0f, 1.0f}));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A CubemapSky shows the face the camera looks at", "[renderer][gl][sky][cubemap]")
{
    for (const auto& [turn, face] : Looks)
    {
        GlScene test;
        test.RequireOpenGL();
        AddTurnedCamera(test, turn[0], turn[1]);
        AddCubemapSky(test);

        const GlProbe::Image image = test.Frame();

        INFO("turned by yaw " << turn[0] << " and pitch " << turn[1] << ", the centre reads face " << face);
        REQUIRE(GlProbe::Near(image.At(Centre, Centre), PixelOf(FaceColours.at(face))));
        REQUIRE(test.messages->errors.empty());
    }
}

TEST_CASE("A cubemap face's top row is up and an opaque quad covers the sky", "[renderer][gl][sky][cubemap]")
{
    GlScene test;
    test.RequireOpenGL();
    test.AddCamera();
    const Rgb bottom{255, 255, 255};
    AddCubemapSky(test, bottom);
    test.AddQuad(0.2f, 2.0f, "builtin:unlit", Fenrir::Colour::Black);

    const GlProbe::Image image = test.Frame();

    INFO("above the centre, the -Z face's top rows show; below it, its bottom rows");
    REQUIRE(GlProbe::Near(image.At(Centre, 8), PixelOf(FaceColours[5])));
    REQUIRE(GlProbe::Near(image.At(Centre, Size - 8), PixelOf(bottom)));
    INFO("the centre, under the black quad, reads the quad");
    REQUIRE(GlProbe::Near(image.At(Centre, Centre), {0.0f, 0.0f, 0.0f, 1.0f}));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A camera's ColourBackground replaces the cubemap sky in its view", "[renderer][gl][sky][cameras]")
{
    GlScene test;
    test.RequireOpenGL();
    test.AddCamera().background = Fenrir::ColourBackground{Fenrir::Colour{0.5f, 0.25f, 0.0f, 1.0f}};
    AddCubemapSky(test);

    const GlProbe::Image image = test.Frame();

    INFO("the view shows the camera's colour, not the sky's -Z face");
    REQUIRE(GlProbe::Near(image.At(Centre, Centre), {0.5f, 0.25f, 0.0f, 1.0f}));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A PanoramaSky shows the part of its panorama the camera looks at", "[renderer][gl][sky][panorama]")
{
    for (const auto& [turn, face] : Looks)
    {
        GlScene test;
        test.RequireOpenGL();
        AddTurnedCamera(test, turn[0], turn[1]);
        AddPanoramaSky(test);

        const GlProbe::Image image = test.Frame();

        INFO("turned by yaw " << turn[0] << " and pitch " << turn[1] << ", the centre reads face " << face
                              << "'s colour, which the panorama holds in that direction");
        REQUIRE(GlProbe::Near(image.At(Centre, Centre), PixelOf(FaceColours.at(face))));
        REQUIRE(test.messages->errors.empty());
    }
}

TEST_CASE("A sky's rotation turns it about +Y and its intensity scales it", "[renderer][gl][sky][panorama]")
{
    const float quarter = std::numbers::pi_v<float> / 2.0f;
    SECTION("a panorama turned a quarter turn shows +X ahead, and -Z to the left")
    {
        for (const auto& [yaw, face] : {std::pair{0.0f, std::size_t{0}}, std::pair{90.0f, std::size_t{5}}})
        {
            GlScene test;
            test.RequireOpenGL();
            AddTurnedCamera(test, yaw, 0.0f);
            AddPanoramaSky(test, 1.0f, quarter);
            INFO("looking along yaw " << yaw);
            REQUIRE(GlProbe::Near(test.Frame().At(Centre, Centre), PixelOf(FaceColours.at(face))));
        }
    }
    SECTION("a cubemap turns the same way")
    {
        GlScene test;
        test.RequireOpenGL();
        test.AddCamera();
        AddCubemapSky(test).As<Fenrir::CubemapSky>().rotation = quarter;
        REQUIRE(GlProbe::Near(test.Frame().At(Centre, Centre), PixelOf(FaceColours[0])));
    }
    SECTION("half the intensity draws half as bright")
    {
        GlScene test;
        test.RequireOpenGL();
        test.AddCamera();
        AddPanoramaSky(test, 0.5f);
        // -Z's yellow, halved.
        REQUIRE(GlProbe::Near(test.Frame().At(Centre, Centre), {0.5f, 0.5f, 0.0f, 1.0f}));
    }
}

TEST_CASE("A panorama sky behind a lit sphere matches its golden image", "[renderer][gl][golden][panorama]")
{
    GlScene test;
    test.RequireOpenGL();
    Fenrir::AssetStore& store = test.assets.Store();
    Fenrir::ImageData data;
    data.width = 96;
    data.height = 64;
    const auto target = store.Add<Fenrir::Image>(std::move(data), Fenrir::AssetOwner::App());
    test.AddCamera().target = Fenrir::ImageTarget{target};

    // A blue sky over a warm horizon and brown ground, with a small sun far over 1, up and to the right.
    const Vec3 sun = glm::normalize(Vec3(0.35f, 0.3f, -1.0f));
    const auto sky = [&](Vec3 direction) {
        if (glm::dot(direction, sun) > std::cos(Fenrir::Math::DegToRad(3.0f)))
        {
            return Vec3(40.0f, 36.0f, 30.0f);
        }
        if (direction.y < 0.0f)
        {
            return Vec3(0.25f, 0.18f, 0.12f) * (1.0f + 0.5f * direction.y);
        }
        return glm::mix(Vec3(1.5f, 1.3f, 1.0f), Vec3(0.15f, 0.35f, 0.9f), std::sqrt(direction.y));
    };
    const auto panorama = store.Add<Fenrir::Image>(Panorama(256, sky), Fenrir::AssetOwner::App());
    test.app.GetActiveScene().AddResource<Fenrir::Sky>(Fenrir::PanoramaSky{panorama});

    // The light travels down, back and to the right, from over the camera's left shoulder, not from the panorama's sun
    // ahead, which would light only the sphere's far side; the sky itself lights nothing until image-based lighting.
    Fenrir::Entity light = test.app.GetActiveScene().GetEntityList().CreateEntity();
    light.GetComponent<Fenrir::Transform>().rot =
        Fenrir::Math::AngleAxis(Fenrir::Math::DegToRad(-30.0f), Vec3(0.0f, 1.0f, 0.0f)) *
        Fenrir::Math::AngleAxis(Fenrir::Math::DegToRad(-40.0f), Vec3(1.0f, 0.0f, 0.0f));
    light.AddComponent<Fenrir::DirectionalLight>(Fenrir::DirectionalLight{Fenrir::Colour::White, 3.0f});
    const auto material = store.CreateMaterial(
        store.Load<Fenrir::Shader>("builtin:lit", Fenrir::AssetOwner::App()).handle, Fenrir::AssetOwner::App());
    store.SetColour(material, "baseColour", Fenrir::Colour{0.9f, 0.9f, 0.9f, 1.0f});
    store.SetFloat(material, "roughness", 0.4f);
    Fenrir::Entity sphere = test.app.GetActiveScene().GetEntityList().CreateEntity();
    sphere.GetComponent<Fenrir::Transform>().pos = Vec3(-0.4f, -0.3f, -3.0f);
    sphere.GetComponent<Fenrir::Transform>().scale = Vec3(1.6f);
    sphere.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{
        store.Add<Fenrir::Mesh>(RendererTest::Sphere(), Fenrir::AssetOwner::App()), material, true});

    test.RunFrame();
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());
    RendererTest::RequireGolden("panorama-sky", test.ReadTarget(target), FENRIR_TEST_GOLDENS,
                                FENRIR_TEST_GOLDEN_FAILURES);
}
