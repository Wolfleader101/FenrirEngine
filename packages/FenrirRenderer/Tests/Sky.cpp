// The scene's Sky and the cameras that override it, drawn by the Background pass (ADR-0026), on the null backend
// through RendererProbe (the spec's seam 1).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <set>
#include <span>
#include <string>
#include <utility>
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

namespace
{
    /** @brief A flat panorama `width` pixels wide and half as high, RGBA32F, one HDR grey. */
    Fenrir::ImageData Panorama(std::uint32_t width)
    {
        return RendererTest::Panorama(width, [](Fenrir::Math::Vec3) { return Fenrir::Math::Vec3(2.0f); });
    }

    /** @brief Panorama(width) with every row repeated, so it is square: not a panorama. */
    Fenrir::ImageData Square(std::uint32_t width)
    {
        Fenrir::ImageData image = Panorama(width);
        image.height = width;
        image.pixels.insert(image.pixels.end(), image.pixels.begin(), image.pixels.end());
        return image;
    }

    /** @brief The last frame's passes that drew a panorama into a cube texture, one per face and mip. */
    std::vector<PassRecord> Conversions(const RendererTestApp& test)
    {
        return RendererTest::Labelled(test.Passes(), "Panorama");
    }

    /** @brief The sky's intensity, and the cosine and sine of its rotation, as the frame block holds them. */
    std::vector<float> FrameSky(const RendererTestApp& test, const PassRecord& background)
    {
        const std::vector<BindingGroupEntry> entries = RendererProbe::Entries(test.Device(), background.groups.at(0));
        REQUIRE(entries.size() == 4);
        const auto bytes = RendererProbe::Bytes(test.Device(), entries[0].buffer);
        // The frame block's fourth vec4, after the light and the time.
        const std::size_t at = entries[0].offset + 48;
        return {RendererTest::FloatAt(bytes, at), RendererTest::FloatAt(bytes, at + 4),
                RendererTest::FloatAt(bytes, at + 8)};
    }
} // namespace

TEST_CASE("A PanoramaSky's passes draw every face and mip of a cube texture that Background then samples",
          "[renderer][sky][panorama]")
{
    RendererTestApp test;
    test.AddCamera();
    test.ActiveScene().AddResource<Fenrir::Sky>(Fenrir::PanoramaSky{test.Image(Panorama(8))});

    test.Frame();
    const std::vector<PassRecord> first = Conversions(test);

    INFO("the Background pass draws with the CUBEMAP variant, as for a CubemapSky");
    REQUIRE(Keys(test, Backgrounds(test).at(0)) == std::vector<std::string>{"CUBEMAP"});
    const Fenrir::Graphics::TextureHandle cube = SkyTexture(test, Backgrounds(test).at(0));
    const Fenrir::Graphics::TextureDesc desc = RendererProbe::Desc(test.Device(), cube);
    INFO("its cube texture is RGBA16F, its faces half as wide as the panorama is high, with every mip");
    REQUIRE(desc.kind == TextureKind::Cube);
    REQUIRE(desc.format == Fenrir::Graphics::TextureFormat::Rgba16Float);
    REQUIRE(desc.width == 2);
    REQUIRE(desc.height == 2);
    REQUIRE(desc.mipLevels == 2);
    INFO("one pass draws each face at each mip into it, at that mip's size");
    REQUIRE(first.size() == 12);
    std::set<std::pair<std::uint32_t, std::uint32_t>> drawn;
    for (const PassRecord& pass : first)
    {
        REQUIRE(pass.pass.colour.size() == 1);
        const Fenrir::Graphics::ColourAttachment& attachment = pass.pass.colour[0];
        REQUIRE(attachment.texture == cube);
        drawn.emplace(attachment.layer, attachment.mipLevel);
        const std::uint32_t side = 2u >> attachment.mipLevel;
        REQUIRE(pass.viewport.width == side);
        REQUIRE(pass.viewport.height == side);
    }
    REQUIRE(drawn.size() == 12);
    INFO("every conversion runs before the Background pass that samples the cube texture");
    const std::vector<PassRecord> passes = test.Passes();
    const auto background = std::find_if(passes.begin(), passes.end(),
                                         [](const PassRecord& pass) { return pass.pass.label == "Background"; });
    REQUIRE(
        std::none_of(background, passes.end(), [](const PassRecord& pass) { return pass.pass.label == "Panorama"; }));
    INFO("the conversion samples the panorama's own texture at set 2, binding 1");
    const Fenrir::Graphics::TextureHandle source =
        RendererProbe::Entries(test.Device(), first[0].groups.at(2)).at(1).texture;
    REQUIRE(RendererProbe::Desc(test.Device(), source).width == 8);
    INFO("nothing was logged");
    REQUIRE(test.messages->errors.empty());
    REQUIRE(test.messages->warnings.empty());
}

TEST_CASE("A PanoramaSky is converted once per image revision, and its image's texture freed after",
          "[renderer][sky][panorama]")
{
    RendererTestApp test;
    test.AddCamera();
    const auto panorama = test.Image(Panorama(8));
    Fenrir::Sky& sky = test.ActiveScene().AddResource<Fenrir::Sky>(Fenrir::PanoramaSky{panorama});
    test.Frame();
    const Fenrir::Graphics::TextureHandle cube = SkyTexture(test, Backgrounds(test).at(0));
    const Fenrir::Graphics::TextureHandle source =
        RendererProbe::Entries(test.Device(), Conversions(test).at(0).groups.at(2)).at(1).texture;

    test.Frame();
    INFO("the next frame draws the same cube texture without converting again, and frees the panorama's texture");
    REQUIRE(Conversions(test).empty());
    REQUIRE(SkyTexture(test, Backgrounds(test).at(0)) == cube);
    REQUIRE_FALSE(test.Device().IsLive(source));

    REQUIRE(test.Store().Replace(panorama, Panorama(16)));
    test.Frame();
    INFO("a new revision of the image converts again, into a cube texture of its own size");
    REQUIRE(Conversions(test).size() == 18);
    const Fenrir::Graphics::TextureHandle replaced = SkyTexture(test, Backgrounds(test).at(0));
    REQUIRE(replaced != cube);
    REQUIRE(RendererProbe::Desc(test.Device(), replaced).width == 4);

    sky = Fenrir::PanoramaSky{test.Image(Panorama(4))};
    test.Frame();
    INFO("another image converts too, once");
    REQUIRE(Conversions(test).size() == 6);
    test.Frame();
    REQUIRE(Conversions(test).empty());
    INFO("nothing was logged, and no command named a freed handle");
    REQUIRE(test.messages->errors.empty());
    REQUIRE(test.messages->warnings.empty());
    REQUIRE(RendererProbe::StaleUses(test.Device()).empty());
}

TEST_CASE("A panorama that is not flat and twice as wide as high draws dark grey, warned once",
          "[renderer][sky][panorama]")
{
    RendererTestApp test;
    test.AddCamera();
    Fenrir::Sky& sky = test.ActiveScene().AddResource<Fenrir::Sky>(Fenrir::PanoramaSky{test.Image(Square(4))});

    test.Frame();
    test.Frame();
    INFO("a square image draws dark grey, warned once over two frames, and is not converted");
    REQUIRE(Keys(test, Backgrounds(test).at(0)).empty());
    REQUIRE(Near(BlockColour(test, Backgrounds(test).at(0)), DarkGrey, DarkGrey, DarkGrey));
    REQUIRE(Conversions(test).empty());
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "Sky") == 1);
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "twice as wide") == 1);

    sky = Fenrir::PanoramaSky{test.Image(Cubemap())};
    test.Frame();
    test.Frame();
    INFO("so does a cubemap");
    REQUIRE(Keys(test, Backgrounds(test).at(0)).empty());
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "Sky") == 2);

    sky = Fenrir::PanoramaSky{};
    test.Frame();
    INFO("and an empty handle");
    REQUIRE(Keys(test, Backgrounds(test).at(0)).empty());
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "Sky") == 3);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A cubemap or panorama sky's intensity and rotation reach the frame block", "[renderer][sky]")
{
    RendererTestApp test;
    test.AddCamera();
    Fenrir::Sky& sky = test.ActiveScene().AddResource<Fenrir::Sky>(Fenrir::CubemapSky{test.Image(Cubemap())});

    test.Frame();
    INFO("by default intensity 1 and no rotation, and the Background pass binds the frame at set 0");
    REQUIRE(RendererProbe::Label(test.Device(), Backgrounds(test).at(0).groups.at(0)) == "Frame");
    REQUIRE(Near(FrameSky(test, Backgrounds(test).at(0)), 1.0f, 1.0f, 0.0f));

    const float quarter = std::numbers::pi_v<float> / 2.0f;
    sky.As<Fenrir::CubemapSky>().intensity = 3.0f;
    sky.As<Fenrir::CubemapSky>().rotation = quarter;
    test.Frame();
    INFO("a CubemapSky's intensity 3 and quarter turn reach the frame block");
    REQUIRE(Near(FrameSky(test, Backgrounds(test).at(0)), 3.0f, std::cos(quarter), 1.0f));

    sky = Fenrir::PanoramaSky{test.Image(Panorama(4)), 0.5f, std::numbers::pi_v<float>};
    test.Frame();
    INFO("a PanoramaSky's intensity 0.5 and half turn reach the frame block");
    REQUIRE(Near(FrameSky(test, Backgrounds(test).at(0)), 0.5f, -1.0f, std::sin(std::numbers::pi_v<float>)));
    REQUIRE(test.messages->errors.empty());
}
