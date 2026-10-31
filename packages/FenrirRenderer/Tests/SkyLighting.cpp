// The scene's Sky filtered into image-based lighting (ADR-0047), on the null backend through RendererProbe (the spec's
// seam 1): which passes run in which frames, and what set 0 binds for the lighting include to read.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "RendererTestApp.hpp"

using Fenrir::RendererProbe;
using Fenrir::Graphics::BindingGroupEntry;
using Fenrir::Graphics::TextureFormat;
using Fenrir::Graphics::TextureHandle;
using Fenrir::Graphics::TextureKind;
using RendererTest::PassRecord;
using RendererTest::RendererTestApp;

namespace
{
    /** @brief A cubemap of six `side` x `side` RGBA8 faces, all one grey. */
    Fenrir::ImageData Cubemap(std::uint32_t side = 4)
    {
        Fenrir::ImageData image;
        image.width = side;
        image.height = side;
        image.shape = Fenrir::ImageShape::Cubemap;
        image.pixels.resize(std::size_t{side} * side * 4 * 6, std::byte{200});
        return image;
    }

    /** @brief How many of the last frame's passes are labelled `label`. */
    std::size_t Count(const RendererTestApp& test, std::string_view label)
    {
        return RendererTest::Labelled(test.Passes(), label).size();
    }

    /** @brief How many passes filter the sky in the last frame: its spherical harmonics and its specular cube's. */
    std::size_t Filterings(const RendererTestApp& test)
    {
        return Count(test, "SkyIrradiance") + Count(test, "SkySpecular");
    }

    /** @brief What the `index`-th Opaque draw of the last frame binds at set 0. */
    std::vector<BindingGroupEntry> FrameSet(const RendererTestApp& test, std::size_t index = 0)
    {
        const std::vector<RendererTest::RecordedDraw> draws = test.Draws();
        REQUIRE(draws.size() > index);
        return RendererProbe::Entries(test.Device(), draws[index].groups.at(0));
    }

    /** @brief The frame block's `skyLight` vec4 (after the light, the time and the drawn sky): x, y, z and w. */
    std::vector<float> FrameSkyLight(const RendererTestApp& test)
    {
        const std::vector<BindingGroupEntry> entries = FrameSet(test);
        const auto bytes = RendererProbe::Bytes(test.Device(), entries.at(0).buffer);
        const std::size_t at = entries.at(0).offset + 64;
        return {RendererTest::FloatAt(bytes, at), RendererTest::FloatAt(bytes, at + 4),
                RendererTest::FloatAt(bytes, at + 8), RendererTest::FloatAt(bytes, at + 12)};
    }

    /** @brief The frame block's `sky` vec4, the drawn sky's: its intensity, and the cosine and sine of its rotation. */
    std::vector<float> FrameSky(const RendererTestApp& test)
    {
        const std::vector<BindingGroupEntry> entries = FrameSet(test);
        const auto bytes = RendererProbe::Bytes(test.Device(), entries.at(0).buffer);
        const std::size_t at = entries.at(0).offset + 48;
        return {RendererTest::FloatAt(bytes, at), RendererTest::FloatAt(bytes, at + 4),
                RendererTest::FloatAt(bytes, at + 8)};
    }

    /** @brief The frame block's `skyLight.w`: the specular cube's highest mip level. */
    float SpecularMaxLevel(const RendererTestApp& test)
    {
        return FrameSkyLight(test).at(3);
    }

    /** @brief A scene with a camera and one lit cube in front of it. */
    void AddLitCube(RendererTestApp& test)
    {
        test.AddCamera();
        test.AddMeshRenderer(test.Mesh(), test.Material());
    }

    /** @brief The position of the first pass labelled `label` in the last frame, or the frame's pass count. */
    std::size_t FirstIndex(const std::vector<PassRecord>& passes, std::string_view label)
    {
        const auto found =
            std::find_if(passes.begin(), passes.end(), [&](const PassRecord& p) { return p.pass.label == label; });
        return static_cast<std::size_t>(found - passes.begin());
    }

    /** @brief The position of the last pass labelled `label` in the last frame. */
    std::size_t LastIndex(const std::vector<PassRecord>& passes, std::string_view label)
    {
        const auto found =
            std::find_if(passes.rbegin(), passes.rend(), [&](const PassRecord& p) { return p.pass.label == label; });
        return static_cast<std::size_t>(passes.rend() - found) - 1;
    }
} // namespace

TEST_CASE(
    "Set 0 binds the frame block, the sky's spherical harmonics, its specular cube, the BRDF table and the lights",
    "[renderer][sky][lighting]")
{
    RendererTestApp test;
    AddLitCube(test);
    test.ActiveScene().AddResource<Fenrir::Sky>(Fenrir::CubemapSky{test.Image(Cubemap())});

    test.Frame();
    const std::vector<BindingGroupEntry> set = FrameSet(test);

    REQUIRE(set.size() == 5);
    INFO("binding 0 is the frame block");
    REQUIRE_FALSE(set[0].buffer.IsEmpty());
    INFO("binding 1 is nine RGBA16F coefficients in a row");
    const auto irradiance = RendererProbe::Desc(test.Device(), set[1].texture);
    REQUIRE(irradiance.format == TextureFormat::Rgba16Float);
    REQUIRE(irradiance.width == 9);
    REQUIRE(irradiance.height == 1);
    INFO("binding 2 is an RGBA16F cube as wide as the sky's 4 x 4 faces, with every mip level");
    const auto specular = RendererProbe::Desc(test.Device(), set[2].texture);
    REQUIRE(specular.kind == TextureKind::Cube);
    REQUIRE(specular.format == TextureFormat::Rgba16Float);
    REQUIRE(specular.width == 4);
    REQUIRE(specular.mipLevels == 3);
    INFO("the frame block's sky.w is its highest mip, roughness 1");
    REQUIRE(SpecularMaxLevel(test) == 2.0f);
    INFO("binding 3 is a 128 x 128 RG16F lookup table");
    const auto brdf = RendererProbe::Desc(test.Device(), set[3].texture);
    REQUIRE(brdf.format == TextureFormat::Rg16Float);
    REQUIRE(brdf.width == 128);
    REQUIRE(brdf.height == 128);
    INFO("binding 4 is the point and spot lights' buffer (Lights.cpp)");
    REQUIRE(set[4].binding == 4);
    REQUIRE_FALSE(set[4].buffer.IsEmpty());
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A cubemap sky is filtered once, into every face and mip, before any view draws", "[renderer][sky][lighting]")
{
    RendererTestApp test;
    AddLitCube(test);
    test.ActiveScene().AddResource<Fenrir::Sky>(Fenrir::CubemapSky{test.Image(Cubemap())});

    test.Frame();
    const std::vector<PassRecord> passes = test.Passes();
    const std::vector<BindingGroupEntry> set = FrameSet(test);

    INFO("one pass projects the spherical harmonics, into the texture set 0 binds");
    const std::vector<PassRecord> irradiance = RendererTest::Labelled(passes, "SkyIrradiance");
    REQUIRE(irradiance.size() == 1);
    REQUIRE(irradiance[0].pass.colour.at(0).texture == set[1].texture);
    INFO("one pass draws each face at each of the specular cube's three mips, at that mip's size");
    const std::vector<PassRecord> specular = RendererTest::Labelled(passes, "SkySpecular");
    REQUIRE(specular.size() == 18);
    std::vector<std::pair<std::uint32_t, std::uint32_t>> drawn;
    for (const PassRecord& pass : specular)
    {
        const auto& attachment = pass.pass.colour.at(0);
        REQUIRE(attachment.texture == set[2].texture);
        drawn.emplace_back(attachment.layer, attachment.mipLevel);
        REQUIRE(pass.viewport.width == (4u >> attachment.mipLevel));
    }
    std::sort(drawn.begin(), drawn.end());
    REQUIRE(std::adjacent_find(drawn.begin(), drawn.end()) == drawn.end());
    INFO("they sample the sky's own cube texture");
    const TextureHandle cube = RendererProbe::Entries(test.Device(), specular[0].groups.at(2)).at(1).texture;
    REQUIRE(RendererProbe::Label(test.Device(), cube) == "Sky cubemap");
    INFO("every filtering pass runs before the Opaque pass");
    REQUIRE(LastIndex(passes, "SkySpecular") < FirstIndex(passes, "Opaque"));
    REQUIRE(LastIndex(passes, "SkyIrradiance") < FirstIndex(passes, "Opaque"));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A filtered sky is kept: later frames filter nothing and bind the same textures", "[renderer][sky][lighting]")
{
    RendererTestApp test;
    AddLitCube(test);
    test.ActiveScene().AddResource<Fenrir::Sky>(Fenrir::CubemapSky{test.Image(Cubemap())});
    test.Frame();
    const std::vector<BindingGroupEntry> set = FrameSet(test);

    test.Frame();

    REQUIRE(Filterings(test) == 0);
    REQUIRE(FrameSet(test)[1].texture == set[1].texture);
    REQUIRE(FrameSet(test)[2].texture == set[2].texture);
}

TEST_CASE("A sky's intensity and rotation never refilter it", "[renderer][sky][lighting]")
{
    RendererTestApp test;
    AddLitCube(test);
    Fenrir::Sky& sky = test.ActiveScene().AddResource<Fenrir::Sky>(Fenrir::CubemapSky{test.Image(Cubemap())});
    test.Frame();
    const TextureHandle first = FrameSet(test)[2].texture;

    sky.As<Fenrir::CubemapSky>().intensity = 4.0f;
    sky.As<Fenrir::CubemapSky>().rotation = 1.0f;
    test.Frame();

    INFO("the lighting include scales by the intensity and turns its lookups by the rotation");
    REQUIRE(Filterings(test) == 0);
    REQUIRE(FrameSet(test)[2].texture == first);
}

TEST_CASE("A new revision of a sky's image filters it again", "[renderer][sky][lighting]")
{
    RendererTestApp test;
    AddLitCube(test);
    const auto image = test.Image(Cubemap());
    test.ActiveScene().AddResource<Fenrir::Sky>(Fenrir::CubemapSky{image});
    test.Frame();
    const TextureHandle first = FrameSet(test)[2].texture;

    REQUIRE(test.Store().Replace(image, Cubemap(8)));
    test.Frame();
    INFO("a new revision of the image filters again, into a new specular cube as wide as its faces");
    REQUIRE(Count(test, "SkyIrradiance") == 1);
    REQUIRE(Count(test, "SkySpecular") == 24);
    REQUIRE(FrameSet(test)[2].texture != first);
    REQUIRE(RendererProbe::Desc(test.Device(), FrameSet(test)[2].texture).width == 8);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Two cameras of one scene share its filtering, a ColourBackground's view lit by the scene's Sky too",
          "[renderer][sky][lighting][cameras]")
{
    RendererTestApp test;
    test.AddCamera();
    Fenrir::Entity second = test.AddCamera({0.0f, 0.0f, 8.0f});
    second.GetComponent<Fenrir::Camera>().background = Fenrir::ColourBackground{Fenrir::Colour::White};
    test.AddMeshRenderer(test.Mesh(), test.Material());
    test.ActiveScene().AddResource<Fenrir::Sky>(Fenrir::PanoramaSky{
        test.Image(RendererTest::Panorama(8, [](Fenrir::Math::Vec3) { return Fenrir::Math::Vec3(1.0f); }))});

    test.Frame();

    INFO("the sky is filtered once in the frame, not once per view");
    REQUIRE(Count(test, "SkyIrradiance") == 1);
    INFO("after the panorama's conversion into its cube");
    const std::vector<PassRecord> passes = test.Passes();
    REQUIRE(LastIndex(passes, "Panorama") < FirstIndex(passes, "SkyIrradiance"));
    INFO("both views' draws bind the same sky textures, the ColourBackground view's included");
    REQUIRE(test.Draws().size() == 2);
    const std::vector<BindingGroupEntry> first = FrameSet(test, 0);
    const std::vector<BindingGroupEntry> other = FrameSet(test, 1);
    REQUIRE(first[1].texture == other[1].texture);
    REQUIRE(first[2].texture == other[2].texture);
    REQUIRE(RendererProbe::Desc(test.Device(), first[2].texture).kind == TextureKind::Cube);
    REQUIRE(RendererProbe::Desc(test.Device(), first[2].texture).width == 2);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("The BRDF lookup table is made once, in the first frame", "[renderer][sky][lighting]")
{
    RendererTestApp test;
    AddLitCube(test);

    test.Frame();
    const std::vector<PassRecord> made = RendererTest::Labelled(test.Passes(), "BrdfLut");
    REQUIRE(made.size() == 1);
    INFO("it draws the table set 0 binds");
    REQUIRE(made[0].pass.colour.at(0).texture == FrameSet(test)[3].texture);

    test.Frame();
    INFO("no later frame makes it again");
    REQUIRE(Count(test, "BrdfLut") == 0);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A ColourSky runs no filtering pass and lights with its colour from a 1 x 1 cube",
          "[renderer][sky][lighting]")
{
    RendererTestApp test;
    AddLitCube(test);
    Fenrir::Sky& sky =
        test.ActiveScene().AddResource<Fenrir::Sky>(Fenrir::ColourSky{Fenrir::Colour{0.2f, 0.3f, 0.4f, 1.0f}});

    test.Frame();
    INFO("no pass filters it");
    REQUIRE(Filterings(test) == 0);
    const std::vector<BindingGroupEntry> set = FrameSet(test);
    INFO("its coefficients and its one-texel specular cube are written from the CPU");
    REQUIRE(RendererProbe::WrittenMips(test.Device(), set[1].texture) == std::vector<bool>{true});
    const auto specular = RendererProbe::Desc(test.Device(), set[2].texture);
    REQUIRE(specular.kind == TextureKind::Cube);
    REQUIRE(specular.width == 1);
    REQUIRE(specular.mipLevels == 1);
    REQUIRE(SpecularMaxLevel(test) == 0.0f);

    test.Frame();
    INFO("the same colour writes nothing new");
    REQUIRE(FrameSet(test)[1].texture == set[1].texture);

    sky.As<Fenrir::ColourSky>().colour = Fenrir::Colour::White;
    test.Frame();
    INFO("a new colour gives new textures");
    REQUIRE(FrameSet(test)[1].texture != set[1].texture);
    REQUIRE(test.messages->errors.empty());
}

namespace
{
    /** @brief The cube texture the last frame's first Background pass draws: its group's binding 0, set 2. */
    TextureHandle DrawnCube(const RendererTestApp& test)
    {
        const std::vector<PassRecord> backgrounds = RendererTest::Labelled(test.Passes(), "Background");
        REQUIRE_FALSE(backgrounds.empty());
        return RendererProbe::Entries(test.Device(), backgrounds[0].groups.at(2)).at(0).texture;
    }
} // namespace

TEST_CASE("A SkyLight's colour lights the scene while its Sky's cubemap is still drawn", "[renderer][sky][lighting]")
{
    RendererTestApp test;
    AddLitCube(test);
    test.ActiveScene().AddResource<Fenrir::Sky>(Fenrir::CubemapSky{test.Image(Cubemap())});
    test.ActiveScene().AddResource<Fenrir::SkyLight>(
        Fenrir::SkyLight{Fenrir::ColourSky{Fenrir::Colour{0.3f, 0.3f, 0.3f, 1.0f}}});

    test.Frame();

    INFO("the Background pass still draws the Sky's cubemap");
    REQUIRE(RendererProbe::Label(test.Device(), DrawnCube(test)) == "Sky cubemap");
    INFO("nothing filters it: set 0 binds the SkyLight colour's one-texel cube");
    REQUIRE(Filterings(test) == 0);
    const auto specular = RendererProbe::Desc(test.Device(), FrameSet(test)[2].texture);
    REQUIRE(specular.kind == TextureKind::Cube);
    REQUIRE(specular.width == 1);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A SkyLight's cubemap is filtered for the light while its ColourSky is drawn", "[renderer][sky][lighting]")
{
    RendererTestApp test;
    AddLitCube(test);
    test.ActiveScene().AddResource<Fenrir::Sky>(Fenrir::ColourSky{Fenrir::Colour::White});
    test.ActiveScene().AddResource<Fenrir::SkyLight>(Fenrir::SkyLight{Fenrir::CubemapSky{test.Image(Cubemap(8))}});

    test.Frame();

    INFO("the SkyLight's cubemap is filtered, into a specular cube as wide as its faces");
    REQUIRE(Count(test, "SkyIrradiance") == 1);
    REQUIRE(RendererProbe::Desc(test.Device(), FrameSet(test)[2].texture).width == 8);
    INFO("the Background pass draws the colour, with no cube");
    const std::vector<PassRecord> backgrounds = RendererTest::Labelled(test.Passes(), "Background");
    REQUIRE(RendererProbe::Keys(test.Device(), backgrounds.at(0).pipeline).empty());
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("The frame block holds the drawn Sky's and the SkyLight's intensity and rotation apart",
          "[renderer][sky][lighting]")
{
    RendererTestApp test;
    AddLitCube(test);
    test.ActiveScene().AddResource<Fenrir::Sky>(Fenrir::CubemapSky{test.Image(Cubemap()), 3.0f, 0.0f});
    test.ActiveScene().AddResource<Fenrir::SkyLight>(
        Fenrir::SkyLight{Fenrir::CubemapSky{test.Image(Cubemap()), 0.5f, std::numbers::pi_v<float>}});

    test.Frame();

    INFO("sky: the drawn Sky's intensity 3 and no turn");
    const std::vector<float> drawn = FrameSky(test);
    REQUIRE(drawn[0] == 3.0f);
    REQUIRE(drawn[1] == 1.0f);
    INFO("skyLight: the SkyLight's intensity 0.5 and half turn");
    const std::vector<float> light = FrameSkyLight(test);
    REQUIRE(light[0] == 0.5f);
    REQUIRE(std::abs(light[1] + 1.0f) < 1e-6f);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("With no SkyLight the scene is lit by its Sky, with the Sky's intensity and rotation",
          "[renderer][sky][lighting]")
{
    RendererTestApp test;
    AddLitCube(test);
    test.ActiveScene().AddResource<Fenrir::Sky>(Fenrir::CubemapSky{test.Image(Cubemap()), 2.0f, 0.0f});

    test.Frame();

    REQUIRE(Count(test, "SkyIrradiance") == 1);
    REQUIRE(FrameSky(test)[0] == 2.0f);
    REQUIRE(FrameSkyLight(test)[0] == 2.0f);
}
