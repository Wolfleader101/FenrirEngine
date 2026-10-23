// The scene's Sky and the cameras that override it, drawn by the Background pass (ADR-0026), on the null backend
// through RendererProbe (the spec's seam 1).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "RendererTestApp.hpp"

using Fenrir::RendererProbe;
using Fenrir::Graphics::BindingGroupEntry;
using Fenrir::Graphics::TextureKind;
using RendererTest::PassRecord;
using RendererTest::RendererTestApp;

namespace
{
    // The sky of a scene with none: a dark grey, linear.
    constexpr float DarkGrey = 0.015f;

    /** @brief A cubemap of six 2 x 2 RGBA8 faces, each one colour. */
    Fenrir::ImageData Cubemap()
    {
        Fenrir::ImageData image;
        image.width = 2;
        image.height = 2;
        image.shape = Fenrir::ImageShape::Cubemap;
        image.pixels.resize(std::size_t{2} * 2 * 4 * 6, std::byte{200});
        return image;
    }

    /** @brief The Background passes of the last frame, one per view, in view order. */
    std::vector<PassRecord> Backgrounds(const RendererTestApp& test)
    {
        return RendererTest::Labelled(test.Passes(), "Background");
    }

    /** @brief The RGB a colour Background pass's block holds. */
    std::vector<float> BlockColour(const RendererTestApp& test, const PassRecord& background)
    {
        const std::vector<BindingGroupEntry> entries = RendererProbe::Entries(test.Device(), background.groups.at(2));
        REQUIRE(entries.size() == 1);
        const auto bytes = RendererProbe::Bytes(test.Device(), entries[0].buffer);
        const std::size_t at = entries[0].offset;
        return {RendererTest::FloatAt(bytes, at), RendererTest::FloatAt(bytes, at + 4),
                RendererTest::FloatAt(bytes, at + 8)};
    }

    bool Near(const std::vector<float>& rgb, float r, float g, float b)
    {
        return rgb.size() == 3 && std::abs(rgb[0] - r) < 1e-6f && std::abs(rgb[1] - g) < 1e-6f &&
               std::abs(rgb[2] - b) < 1e-6f;
    }

    /** @brief The texture a cubemap Background pass binds at set 2, binding 0. */
    Fenrir::Graphics::TextureHandle SkyTexture(const RendererTestApp& test, const PassRecord& background)
    {
        const std::vector<BindingGroupEntry> entries = RendererProbe::Entries(test.Device(), background.groups.at(2));
        REQUIRE(entries.size() == 1);
        return entries[0].texture;
    }

    std::vector<std::string> Keys(const RendererTestApp& test, const PassRecord& pass)
    {
        return RendererProbe::Keys(test.Device(), pass.pipeline);
    }
} // namespace

TEST_CASE("With no Sky the Background pass fills with dark grey", "[renderer][sky]")
{
    RendererTestApp test;
    test.AddCamera();

    test.Frame();
    const std::vector<PassRecord> backgrounds = Backgrounds(test);

    INFO("one Background pass, the colour variant, holding dark grey");
    REQUIRE(backgrounds.size() == 1);
    REQUIRE(Keys(test, backgrounds[0]).empty());
    REQUIRE(Near(BlockColour(test, backgrounds[0]), DarkGrey, DarkGrey, DarkGrey));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A ColourSky is the colour the Background pass fills with", "[renderer][sky]")
{
    RendererTestApp test;
    test.AddCamera();
    test.ActiveScene().AddResource<Fenrir::Sky>(Fenrir::ColourSky{Fenrir::Colour{0.2f, 0.4f, 0.8f, 1.0f}});

    test.Frame();
    const std::vector<PassRecord> backgrounds = Backgrounds(test);

    INFO("the colour variant draws the sky's colour");
    REQUIRE(backgrounds.size() == 1);
    REQUIRE(Keys(test, backgrounds[0]).empty());
    REQUIRE(Near(BlockColour(test, backgrounds[0]), 0.2f, 0.4f, 0.8f));
}

TEST_CASE("A CubemapSky draws with the CUBEMAP variant, sampling its cube texture by the view",
          "[renderer][sky][cubemap]")
{
    RendererTestApp test;
    test.AddCamera();
    const auto cubemap = test.Image(Cubemap());
    test.ActiveScene().AddResource<Fenrir::Sky>(Fenrir::CubemapSky{cubemap});

    test.Frame();
    const std::vector<PassRecord> backgrounds = Backgrounds(test);

    INFO("the Background pass draws with the CUBEMAP variant");
    REQUIRE(backgrounds.size() == 1);
    REQUIRE(Keys(test, backgrounds[0]) == std::vector<std::string>{"CUBEMAP"});
    INFO("it binds the view at set 1, as a material's draws do, for the direction each pixel looks along");
    REQUIRE(RendererProbe::Label(test.Device(), backgrounds[0].groups.at(1)) == "View");
    INFO("set 2 binds a cube texture of the cubemap's 2 x 2 faces, with its mips, and a sampler");
    const Fenrir::Graphics::TextureHandle texture = SkyTexture(test, backgrounds[0]);
    const Fenrir::Graphics::TextureDesc desc = RendererProbe::Desc(test.Device(), texture);
    REQUIRE(desc.kind == TextureKind::Cube);
    REQUIRE(desc.width == 2);
    REQUIRE(desc.height == 2);
    REQUIRE(desc.format == Fenrir::Graphics::TextureFormat::Rgba8Srgb);
    REQUIRE(desc.mipLevels == 2);
    REQUIRE(RendererProbe::WrittenMips(test.Device(), texture) == std::vector<bool>{true, true});
    REQUIRE_FALSE(RendererProbe::Entries(test.Device(), backgrounds[0].groups.at(2))[0].sampler.IsEmpty());
    REQUIRE(test.messages->errors.empty());
    REQUIRE(test.messages->warnings.empty());
}

TEST_CASE("A camera's ColourBackground overrides the scene's Sky for its view only", "[renderer][sky][cameras]")
{
    RendererTestApp test;
    Fenrir::Entity left = test.AddCamera();
    left.GetComponent<Fenrir::Camera>().viewport = {0.0f, 0.0f, 0.5f, 1.0f};
    Fenrir::Entity right = test.AddCamera();
    Fenrir::Camera& overriding = right.GetComponent<Fenrir::Camera>();
    overriding.viewport = {0.5f, 0.0f, 0.5f, 1.0f};
    overriding.order = 1;
    overriding.background = Fenrir::ColourBackground{Fenrir::Colour{0.9f, 0.1f, 0.3f, 1.0f}};
    test.ActiveScene().AddResource<Fenrir::Sky>(Fenrir::CubemapSky{test.Image(Cubemap())});

    test.Frame();
    const std::vector<PassRecord> backgrounds = Backgrounds(test);

    REQUIRE(backgrounds.size() == 2);
    INFO("the first view, on the scene's background, draws the cubemap");
    REQUIRE(Keys(test, backgrounds[0]) == std::vector<std::string>{"CUBEMAP"});
    INFO("the second view, overriding it, fills with its own colour");
    REQUIRE(Keys(test, backgrounds[1]).empty());
    REQUIRE(Near(BlockColour(test, backgrounds[1]), 0.9f, 0.1f, 0.3f));
}

TEST_CASE("An empty, stale or flat Sky cubemap draws dark grey, logged once each", "[renderer][sky][cubemap]")
{
    RendererTestApp test;
    test.AddCamera();
    Fenrir::Sky& sky = test.ActiveScene().AddResource<Fenrir::Sky>(Fenrir::CubemapSky{});

    test.Frame();
    test.Frame();
    INFO("an empty cubemap fills with dark grey, logged once over two frames");
    REQUIRE(Keys(test, Backgrounds(test).at(0)).empty());
    REQUIRE(Near(BlockColour(test, Backgrounds(test).at(0)), DarkGrey, DarkGrey, DarkGrey));
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "Sky") == 1);

    const auto stale = test.Image(Cubemap());
    REQUIRE(test.Store().Unload(stale));
    sky = Fenrir::CubemapSky{stale};
    test.Frame();
    test.Frame();
    INFO("a stale cubemap does the same, logged once more");
    REQUIRE(Keys(test, Backgrounds(test).at(0)).empty());
    REQUIRE(Near(BlockColour(test, Backgrounds(test).at(0)), DarkGrey, DarkGrey, DarkGrey));
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "Sky") == 2);

    sky = Fenrir::CubemapSky{test.Image(RendererTest::Picture(1, 1, {255, 255, 255, 255}))};
    test.Frame();
    INFO("so does a flat image, which is not a cubemap");
    REQUIRE(Keys(test, Backgrounds(test).at(0)).empty());
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "Sky") == 3);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A Sky cubemap replaced with new faces is uploaded again", "[renderer][sky][cubemap]")
{
    RendererTestApp test;
    test.AddCamera();
    const auto cubemap = test.Image(Cubemap());
    test.ActiveScene().AddResource<Fenrir::Sky>(Fenrir::CubemapSky{cubemap});
    test.Frame();
    const Fenrir::Graphics::TextureHandle first = SkyTexture(test, Backgrounds(test).at(0));

    Fenrir::ImageData bigger = Cubemap();
    bigger.width = 4;
    bigger.height = 4;
    bigger.pixels.resize(std::size_t{4} * 4 * 4 * 6, std::byte{100});
    REQUIRE(test.Store().Replace(cubemap, bigger));
    test.Frame();
    const Fenrir::Graphics::TextureHandle second = SkyTexture(test, Backgrounds(test).at(0));

    INFO("the new revision binds a new cube texture of the new size");
    REQUIRE(second != first);
    REQUIRE(RendererProbe::Desc(test.Device(), second).width == 4);
}
