// The scene's Sky on OpenGL (the spec's seam 2): the Background pass's colour or cubemap in the pixels nothing drew,
// read back from the view's SceneColor.

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirMath/Math.hpp"
#include "GlScene.hpp"

using Fenrir::GlProbe;
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

    void AddCubemapSky(GlScene& test, const Rgb& bottom = FaceColours[5])
    {
        const auto cubemap = test.assets.Store().Add<Fenrir::Image>(Cubemap(bottom), Fenrir::AssetOwner::App());
        test.app.GetActiveScene().AddResource<Fenrir::Sky>(Fenrir::CubemapSky{cubemap});
    }
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
    // Looking along -Z, then turned to look along +X, -X, +Y and -Y.
    const std::array<std::pair<std::array<float, 2>, std::size_t>, 5> looks{{
        {{0.0f, 0.0f}, 5},
        {{-90.0f, 0.0f}, 0},
        {{90.0f, 0.0f}, 1},
        {{0.0f, 90.0f}, 2},
        {{0.0f, -90.0f}, 3},
    }};
    for (const auto& [turn, face] : looks)
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
