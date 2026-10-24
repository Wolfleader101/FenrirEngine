// Cameras on OpenGL (the spec's seam 2): image targets drawn by one camera and sampled by another's material,
// viewport rectangles, and MSAA resolved in Output (ADR-0026, ADR-0019). Image targets are RGBA8 sRGB and are read back
// as stored, so the colours written into them are 0 or 1 in each channel, which sRGB encoding leaves alone.

#include <catch2/catch_test_macros.hpp>

#include "GlScene.hpp"

using Fenrir::GlProbe;
using Fenrir::RendererProbe;
using GlTest::GlScene;

namespace
{
    constexpr std::uint32_t Size = GlScene::Size;

    /** @brief A render target image of `width` x `height`. */
    Fenrir::AssetHandle<Fenrir::Image> TargetImage(GlScene& test, std::uint32_t width, std::uint32_t height)
    {
        Fenrir::ImageData data;
        data.width = width;
        data.height = height;
        return test.assets.Store().Add<Fenrir::Image>(std::move(data), Fenrir::AssetOwner::App());
    }

    /** @brief The pixels of a render target image the last frame drew. */
    GlProbe::Image ReadImage(GlScene& test, Fenrir::AssetHandle<Fenrir::Image> image)
    {
        const Fenrir::Graphics::TextureHandle texture = RendererProbe::ImageTexture(test.renderer, image);
        REQUIRE_FALSE(texture.IsEmpty());
        return GlProbe::Read(RendererProbe::Device(test.renderer), texture);
    }

    /** @brief A material on the test shader SampledPicture, sampling `image`. */
    Fenrir::AssetHandle<Fenrir::Material> SampledMaterial(GlScene& test, Fenrir::AssetHandle<Fenrir::Image> image)
    {
        Fenrir::AssetStore& store = test.assets.Store();
        Fenrir::ShaderData data;
        const auto bytes = std::as_bytes(std::span(Fenrir::Shaders::SampledPicture));
        data.bytes.assign(bytes.begin(), bytes.end());
        const auto shader = store.Add<Fenrir::Shader>(std::move(data), Fenrir::AssetOwner::App());
        const auto material = store.CreateMaterial(shader, Fenrir::AssetOwner::App());
        REQUIRE(store.SetTexture(material, "picture", image));
        return material;
    }
} // namespace

TEST_CASE("An image one camera draws is what a material sampling it reads", "[renderer][gl][cameras]")
{
    GlScene test;
    test.RequireOpenGL();
    const auto image = TargetImage(test, 32, 32);
    // The image's camera looks away from the quad (along +Z), so its view is its background: a linear grey that the
    // image stores sRGB-encoded and sampling decodes back.
    Fenrir::Entity imageCamera = test.app.GetActiveScene().GetEntityList().CreateEntity();
    imageCamera.GetComponent<Fenrir::Transform>().rot =
        Fenrir::Math::AngleAxis(3.14159265f, Fenrir::Math::Vec3(0.0f, 1.0f, 0.0f));
    Fenrir::Camera& camera = imageCamera.AddComponent<Fenrir::Camera>();
    camera.target = Fenrir::ImageTarget{image};
    camera.background = Fenrir::ColourBackground{Fenrir::Colour{0.5f, 0.5f, 0.5f, 1.0f}};
    test.AddCamera();
    test.AddQuad(1.0f, 2.0f, SampledMaterial(test, image));

    const GlProbe::Image window = test.Frame(Size, Size);

    INFO("the window's view shows the quad holding the image's grey, linear 0.5 after the sRGB round trip");
    REQUIRE(GlProbe::Near(window.At(Size / 2, Size / 2), {0.5f, 0.5f, 0.5f, 1.0f}, 0.01f));
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Two cameras into one image each write only their viewport", "[renderer][gl][cameras]")
{
    GlScene test;
    test.RequireOpenGL();
    const auto image = TargetImage(test, 32, 16);
    Fenrir::Camera& left = test.AddCamera();
    left.target = Fenrir::ImageTarget{image};
    left.viewport = {0.0f, 0.0f, 0.5f, 1.0f};
    left.background = Fenrir::ColourBackground{Fenrir::Colour::Red};
    Fenrir::Camera& bottomRight = test.AddCamera();
    bottomRight.target = Fenrir::ImageTarget{image};
    bottomRight.viewport = {0.5f, 0.5f, 0.5f, 0.5f};
    bottomRight.background = Fenrir::ColourBackground{Fenrir::Colour::Blue};

    test.RunFrame();
    const GlProbe::Image pixels = ReadImage(test, image);

    INFO("the left half is red");
    REQUIRE(GlProbe::Near(pixels.At(4, 2), {1.0f, 0.0f, 0.0f, 1.0f}));
    REQUIRE(GlProbe::Near(pixels.At(12, 13), {1.0f, 0.0f, 0.0f, 1.0f}));
    INFO("the bottom right quarter is blue, measured from the top-left");
    REQUIRE(GlProbe::Near(pixels.At(28, 13), {0.0f, 0.0f, 1.0f, 1.0f}));
    INFO("the top right quarter, which no view wrote, holds the first view's clear: black");
    REQUIRE(GlProbe::Near(pixels.At(28, 2), {0.0f, 0.0f, 0.0f, 1.0f}));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("With msaa 4 the scene renders and Output resolves it with no GL errors", "[renderer][gl][msaa]")
{
    GlScene test(4);
    test.RequireOpenGL();
    const auto image = TargetImage(test, Size, Size);
    Fenrir::Camera& intoImage = test.AddCamera();
    intoImage.target = Fenrir::ImageTarget{image};
    intoImage.background = Fenrir::ColourBackground{Fenrir::Colour::Green};
    // And one into the window, resolved into the backbuffer.
    test.AddCamera();
    test.AddQuad(1.0f, 2.0f, "builtin:unlit", Fenrir::Colour::Red);

    test.RunFrame();
    test.RunFrame();
    const GlProbe::Image pixels = ReadImage(test, image);

    INFO("the resolved image holds the quad's red at the centre and the green background at a corner");
    REQUIRE(GlProbe::Near(pixels.At(Size / 2, Size / 2), {1.0f, 0.0f, 0.0f, 1.0f}));
    REQUIRE(GlProbe::Near(pixels.At(1, 1), {0.0f, 1.0f, 0.0f, 1.0f}));
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Two views into the window each draw their opaque items over their background", "[renderer][gl][cameras]")
{
    GlScene test;
    test.RequireOpenGL();
    Fenrir::Camera& left = test.AddCamera();
    left.viewport = {0.0f, 0.0f, 0.25f, 1.0f};
    Fenrir::Camera& right = test.AddCamera();
    right.viewport = {0.25f, 0.0f, 0.75f, 1.0f};
    test.AddQuad(1.0f, 2.0f, "builtin:unlit", Fenrir::Colour::Red);

    test.RunFrame();
    Fenrir::Graphics::GraphicsDevice& device = RendererProbe::Device(test.renderer);
    const GlProbe::Image first = GlProbe::Read(device, RendererProbe::SceneColour(test.renderer, Size / 4, Size));
    const GlProbe::Image second = GlProbe::Read(device, RendererProbe::SceneColour(test.renderer, Size * 3 / 4, Size));

    // The first view's Background runs after the second view's Opaque pass, back on the first view's SceneDepth.
    INFO("the quad covers the centre of both views' SceneColor, the first's included");
    REQUIRE(GlProbe::Near(first.At(Size / 8, Size / 2), {1.0f, 0.0f, 0.0f, 1.0f}));
    REQUIRE(GlProbe::Near(second.At(Size * 3 / 8, Size / 2), {1.0f, 0.0f, 0.0f, 1.0f}));
    REQUIRE(test.messages->errors.empty());
}
