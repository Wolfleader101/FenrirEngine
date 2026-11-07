// Lens flare and lens dirt (ADR-0059): where a pixel reads its ghosts and where a bright point shows them, how lens
// dirt scales a pixel's share of bloom and the flare, and the generated dirt image, on the CPU; and on the null backend
// (the spec's seam 1), each view's FlareGhosts, FlareBlur and FlareComposite passes after its streaks, their blocks and
// blending, none while the flare is off, and the DIRT composites that read a camera's dirt image.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/Bloom.hpp"
#include "Core/LensFlare.hpp"
#include "FenrirRenderer/LensDirt.hpp"
#include "RendererTestApp.hpp"

using Fenrir::RendererProbe;
using Fenrir::Graphics::BlendMode;
using Fenrir::Graphics::LoadOp;
using Fenrir::Graphics::PixelRect;
using Fenrir::Graphics::ResourceUsage;
using Fenrir::Graphics::TextureHandle;
using Fenrir::Math::Vec2;
using Fenrir::Render::FlareLevel;
using Fenrir::Render::GhostPositions;
using Fenrir::Render::GhostReadPoint;
using Fenrir::Render::LensDirtShare;
using Fenrir::Render::MaxGhosts;
using RendererTest::PassRecord;
using RendererTest::RendererTestApp;

namespace
{
    bool Near(float a, float b, float tolerance = 1e-5f)
    {
        return std::abs(a - b) < tolerance;
    }

    /** @brief The cross product of `a - centre` and `b - centre`: 0 when the three lie on one line. */
    float Cross(Vec2 a, Vec2 b)
    {
        const Vec2 centre(0.5f);
        return (a.x - centre.x) * (b.y - centre.y) - (a.y - centre.y) * (b.x - centre.x);
    }

    /** @brief What a bloom pass's block holds, as builtin:bloom-upsample declares it. */
    struct BloomBlock
    {
        float texelX = 0.0f;
        float texelY = 0.0f;
        float blend = 0.0f;
        float dirtIntensity = 0.0f;
    };

    /** @brief What a FlareGhosts pass's block holds, as builtin:lens-flare declares it. */
    struct FlareBlock
    {
        float ghostSpacing = 0.0f;
        float haloRadius = 0.0f;
        float haloWidth = 0.0f;
        float chromaticOffset = 0.0f;
        std::uint32_t ghostCount = 0;
        float aspect = 0.0f;
    };

    std::vector<std::byte> BlockBytes(const RendererTestApp& test, const PassRecord& pass, std::size_t& at)
    {
        const auto entries = RendererProbe::Entries(test.Device(), pass.groups.at(1));
        REQUIRE(entries.size() == 1);
        at = entries[0].offset;
        return RendererProbe::Bytes(test.Device(), entries[0].buffer);
    }

    BloomBlock BloomBlockOf(const RendererTestApp& test, const PassRecord& pass)
    {
        std::size_t at = 0;
        const auto bytes = BlockBytes(test, pass, at);
        return {RendererTest::FloatAt(bytes, at), RendererTest::FloatAt(bytes, at + 4),
                RendererTest::FloatAt(bytes, at + 8), RendererTest::FloatAt(bytes, at + 28)};
    }

    FlareBlock FlareBlockOf(const RendererTestApp& test, const PassRecord& pass)
    {
        std::size_t at = 0;
        const auto bytes = BlockBytes(test, pass, at);
        return {RendererTest::FloatAt(bytes, at),     RendererTest::FloatAt(bytes, at + 4),
                RendererTest::FloatAt(bytes, at + 8), RendererTest::FloatAt(bytes, at + 12),
                RendererTest::UintAt(bytes, at + 16), RendererTest::FloatAt(bytes, at + 20)};
    }

    bool Samples(const PassRecord& pass, TextureHandle texture)
    {
        return std::any_of(pass.pass.uses.begin(), pass.pass.uses.end(), [&](const auto& use) {
            return use.texture == texture && use.usage == ResourceUsage::Sampled;
        });
    }

    /** @brief How many textures the pass samples. */
    std::size_t SampledCount(const PassRecord& pass)
    {
        return static_cast<std::size_t>(
            std::count_if(pass.pass.uses.begin(), pass.pass.uses.end(),
                          [](const auto& use) { return use.usage == ResourceUsage::Sampled; }));
    }

    std::vector<std::string> Labels(const std::vector<PassRecord>& passes)
    {
        std::vector<std::string> labels;
        for (const PassRecord& pass : passes)
        {
            labels.push_back(pass.pass.label);
        }
        return labels;
    }

    /** @brief A camera with a mesh in front of it, its flare on at `intensity`. */
    Fenrir::Camera& AddFlaringCamera(RendererTestApp& test, float intensity)
    {
        Fenrir::Camera& camera = test.AddCamera().GetComponent<Fenrir::Camera>();
        camera.lensFlare.intensity = intensity;
        test.AddMeshRenderer(test.Mesh(), test.Material());
        return camera;
    }

    /** @brief A flat, linear 4 x 4 image, every pixel `value`. */
    Fenrir::AssetHandle<Fenrir::Image> AddDirt(RendererTestApp& test, std::uint8_t value)
    {
        return test.Store().Add<Fenrir::Image>(
            RendererTest::Picture(4, 4, std::vector<std::uint8_t>(64, value), Fenrir::ColourSpace::Linear),
            Fenrir::AssetOwner::App());
    }
} // namespace

TEST_CASE("A pixel reads each ghost further along its line through the view's centre", "[renderer][flare]")
{
    const Vec2 uv(0.8f, 0.3f);
    INFO("ghost 1 at spacing 0.5 is read half way to the centre, ghost 2 at the centre, ghost 3 as far past it");
    const auto requireAt = [](Vec2 point, Vec2 expected) {
        INFO("(" << point.x << ", " << point.y << ") against (" << expected.x << ", " << expected.y << ")");
        REQUIRE(Near(point.x, expected.x));
        REQUIRE(Near(point.y, expected.y));
    };
    requireAt(GhostReadPoint(uv, 1, 0.5f), Vec2(0.65f, 0.4f));
    requireAt(GhostReadPoint(uv, 2, 0.5f), Vec2(0.5f));
    requireAt(GhostReadPoint(uv, 3, 0.5f), Vec2(0.35f, 0.6f));
    INFO("the centre reads itself for every ghost");
    REQUIRE(GhostReadPoint(Vec2(0.5f), 3, 0.4f) == Vec2(0.5f));
}

TEST_CASE("A bright point's ghosts lie on the line through it and the view's centre", "[renderer][flare]")
{
    const Vec2 source(0.7f, 0.4f);
    for (const std::uint32_t count : {1u, 4u, 8u})
    {
        for (const float spacing : {0.3f, 0.4f, 0.7f})
        {
            const std::vector<Vec2> ghosts = GhostPositions(source, count, spacing);
            INFO(count << " ghosts at spacing " << spacing);
            REQUIRE(ghosts.size() == count);
            for (std::uint32_t i = 0; i < count; ++i)
            {
                INFO("ghost " << i + 1 << " at (" << ghosts[i].x << ", " << ghosts[i].y << ")");
                REQUIRE(Near(Cross(source, ghosts[i]), 0.0f));
                INFO("and the pixel there reads that ghost from the bright point");
                const Vec2 read = GhostReadPoint(ghosts[i], i + 1, spacing);
                REQUIRE(Near(read.x, source.x, 1e-4f));
                REQUIRE(Near(read.y, source.y, 1e-4f));
            }
        }
    }

    const std::vector<Vec2> ghosts = GhostPositions(source, 4, 0.4f);
    INFO("ghosts read less than all the way to the centre land on the point's side of it, further out; those read "
         "past it land on the far side");
    REQUIRE(ghosts[0].x > source.x);
    REQUIRE(ghosts[1].x > ghosts[0].x);
    REQUIRE(ghosts[2].x < 0.5f);
    REQUIRE(ghosts[3].x < 0.5f);
    REQUIRE(ghosts[3].x > ghosts[2].x);
    INFO("a ghost read exactly at the centre fills the view, so it has no place and is left out");
    REQUIRE(GhostPositions(source, 3, 0.5f).size() == 2);
    INFO("the count is held to MaxGhosts");
    REQUIRE(GhostPositions(source, 100, 0.3f).size() == MaxGhosts);
}

TEST_CASE("Lens dirt raises a pixel's share of bloom and the flare, never past 1", "[renderer][flare]")
{
    INFO("no dirt, or no dirt intensity, keeps the share");
    REQUIRE(LensDirtShare(0.04f, 3.0f, 0.0f) == 0.04f);
    REQUIRE(LensDirtShare(0.04f, 0.0f, 1.0f) == 0.04f);
    INFO("white dirt at intensity 1 doubles it, grey at intensity 4 triples it");
    REQUIRE(Near(LensDirtShare(0.04f, 1.0f, 1.0f), 0.08f));
    REQUIRE(Near(LensDirtShare(0.04f, 4.0f, 0.5f), 0.12f));
    for (const float share : {0.0f, 0.04f, 0.3f, 0.9f, 1.0f})
    {
        for (const float intensity : {0.0f, 1.0f, 10.0f, 1000.0f})
        {
            for (const float dirt : {0.0f, 0.25f, 1.0f})
            {
                const float dirty = LensDirtShare(share, intensity, dirt);
                INFO("share " << share << ", dirt intensity " << intensity << ", dirt " << dirt << ": " << dirty);
                REQUIRE(dirty <= 1.0f);
                REQUIRE(dirty >= share);
            }
        }
    }
    INFO("a negative dirt intensity or dirt is none");
    REQUIRE(LensDirtShare(0.5f, -2.0f, 1.0f) == 0.5f);
    REQUIRE(LensDirtShare(0.5f, 2.0f, -1.0f) == 0.5f);
}

TEST_CASE("The flare reads bloom's second level, or its first when the chain has one", "[renderer][flare]")
{
    REQUIRE(FlareLevel({1280, 720}) == 1);
    REQUIRE(FlareLevel({4, 4}) == 1);
    REQUIRE(FlareLevel({2, 3}) == 0);
}

TEST_CASE("The generated lens dirt is greyscale, mostly clean, and the same for the same seed", "[renderer][flare]")
{
    const Fenrir::ImageData dirt = Fenrir::GenerateLensDirt(256, 128, 7);
    REQUIRE(dirt.width == 256);
    REQUIRE(dirt.height == 128);
    REQUIRE(dirt.format == Fenrir::ImageFormat::RGBA8);
    REQUIRE(dirt.colourSpace == Fenrir::ColourSpace::Linear);
    REQUIRE(dirt.pixels.size() == std::size_t{256} * 128 * 4);

    double sum = 0.0;
    std::uint32_t bright = 0;
    bool grey = true;
    for (std::size_t p = 0; p < dirt.pixels.size(); p += 4)
    {
        const auto r = std::to_integer<std::uint32_t>(dirt.pixels[p]);
        grey = grey && r == std::to_integer<std::uint32_t>(dirt.pixels[p + 1]) &&
               r == std::to_integer<std::uint32_t>(dirt.pixels[p + 2]) &&
               std::to_integer<std::uint32_t>(dirt.pixels[p + 3]) == 255;
        sum += r / 255.0;
        bright += r > 128 ? 1 : 0;
    }
    const double mean = sum / (256.0 * 128.0);
    INFO("every pixel is grey and opaque");
    REQUIRE(grey);
    INFO("the mean is " << mean << " and " << bright
                        << " pixels are over half bright: a mostly clean lens with some "
                           "dirt on it");
    REQUIRE(mean > 0.02);
    REQUIRE(mean < 0.35);
    REQUIRE(bright > 0);
    INFO("the same seed makes the same image, another seed another");
    REQUIRE(Fenrir::GenerateLensDirt(256, 128, 7).pixels == dirt.pixels);
    REQUIRE(Fenrir::GenerateLensDirt(256, 128, 8).pixels != dirt.pixels);
}

TEST_CASE("A camera's lens flare is off by default and adds no pass", "[renderer][flare]")
{
    RendererTestApp test;
    test.AddCamera();
    test.AddMeshRenderer(test.Mesh(), test.Material());
    INFO("a camera's flare intensity is 0 by default, and it has no dirt");
    REQUIRE(Fenrir::Camera{}.lensFlare.intensity == 0.0f);
    REQUIRE(Fenrir::Camera{}.lensFlare.dirt.IsEmpty());

    test.Frame();
    const std::vector<std::string> labels = Labels(test.Passes());
    REQUIRE(std::none_of(labels.begin(), labels.end(),
                         [](const std::string& label) { return label.rfind("Flare", 0) == 0; }));
    INFO("and bloom's composite is the plain one");
    REQUIRE(RendererProbe::Keys(test.Device(), RendererTest::Labelled(test.Passes(), "BloomComposite").at(0).pipeline)
                .empty());
}

TEST_CASE("A camera's flare runs in PostProcess after its streaks, from bloom's second level back into SceneColor",
          "[renderer][flare]")
{
    RendererTestApp test;
    Fenrir::Camera& camera = AddFlaringCamera(test, 0.05f);
    camera.anamorphicStreaks.intensity = 0.1f;
    camera.lensFlare.ghostCount = 5;
    camera.lensFlare.ghostSpacing = 0.3f;
    camera.lensFlare.haloRadius = 0.55f;
    camera.lensFlare.haloWidth = 0.2f;
    camera.lensFlare.chromaticOffset = 0.02f;

    test.Frame();
    const std::vector<PassRecord> passes = test.Passes();
    std::vector<std::string> expected{"BrdfLut", "DepthPrepass", "Opaque", "Background"};
    expected.insert(expected.end(), 6, "BloomDownsample");
    expected.insert(expected.end(), 8, "StreakDownsample");
    expected.insert(expected.end(), 5, "BloomUpsample");
    expected.emplace_back("BloomComposite");
    expected.insert(expected.end(), 7, "StreakUpsample");
    expected.emplace_back("StreakComposite");
    expected.insert(expected.end(), {"FlareGhosts", "FlareBlur", "FlareComposite", "Output"});
    INFO("the flare's three passes follow the streaks' composite");
    REQUIRE(Labels(passes) == expected);

    const TextureHandle sceneColour = RendererTest::Labelled(passes, "Opaque").at(0).pass.colour.at(0).texture;
    const TextureHandle bloomLevel1 = RendererTest::Labelled(passes, "BloomDownsample").at(1).pass.colour.at(0).texture;
    const PassRecord ghosts = RendererTest::Labelled(passes, "FlareGhosts").at(0);
    const PassRecord blur = RendererTest::Labelled(passes, "FlareBlur").at(0);
    const PassRecord composite = RendererTest::Labelled(passes, "FlareComposite").at(0);

    INFO("the ghosts read bloom's level 1, 320 x 180, into a level of that size, with the camera's settings");
    REQUIRE(Samples(ghosts, bloomLevel1));
    REQUIRE(ghosts.viewport == PixelRect{0, 0, 320, 180});
    REQUIRE(ghosts.pass.colour.at(0).load == LoadOp::DontCare);
    REQUIRE(RendererProbe::State(test.Device(), ghosts.pipeline).blend == BlendMode::None);
    const FlareBlock block = FlareBlockOf(test, ghosts);
    REQUIRE(Near(block.ghostSpacing, 0.3f));
    REQUIRE(Near(block.haloRadius, 0.55f));
    REQUIRE(Near(block.haloWidth, 0.2f));
    REQUIRE(Near(block.chromaticOffset, 0.02f));
    REQUIRE(block.ghostCount == 5);
    REQUIRE(Near(block.aspect, 1280.0f / 720.0f));

    INFO("the blur is bloom's 13-tap filter at the same size, without the Karis average");
    REQUIRE(Samples(blur, ghosts.pass.colour.at(0).texture));
    REQUIRE(blur.viewport == PixelRect{0, 0, 320, 180});
    REQUIRE(blur.pass.colour.at(0).texture != ghosts.pass.colour.at(0).texture);
    REQUIRE(Near(BloomBlockOf(test, blur).texelX, 1.0f / 320.0f));
    REQUIRE(RendererProbe::Keys(test.Device(), blur.pipeline).empty());

    INFO("the composite blends the blurred flare over the whole of SceneColor by the intensity, as bloom's does");
    REQUIRE(Samples(composite, blur.pass.colour.at(0).texture));
    REQUIRE(composite.pass.colour.at(0).texture == sceneColour);
    REQUIRE(composite.pass.colour.at(0).load == LoadOp::Load);
    REQUIRE(composite.viewport == PixelRect{0, 0, 1280, 720});
    REQUIRE(Near(BloomBlockOf(test, composite).blend, 0.05f));
    REQUIRE(Near(BloomBlockOf(test, composite).texelX, 1.0f / 320.0f));
    REQUIRE(RendererProbe::State(test.Device(), composite.pipeline).blend == BlendMode::Alpha);
    REQUIRE(RendererProbe::Keys(test.Device(), composite.pipeline).empty());
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A camera with bloom off still flares, running bloom's first two downsamples for itself", "[renderer][flare]")
{
    RendererTestApp test;
    Fenrir::Camera& camera = AddFlaringCamera(test, 3.0f);
    camera.bloom.intensity = 0.0f;
    camera.lensFlare.ghostCount = 50;

    test.Frame();
    const std::vector<PassRecord> passes = test.Passes();
    INFO("two downsamples of SceneColor down to level 1, then the flare's passes; no bloom upsample or composite");
    REQUIRE(Labels(passes) == std::vector<std::string>{"BrdfLut", "DepthPrepass", "Opaque", "Background",
                                                       "BloomDownsample", "BloomDownsample", "FlareGhosts", "FlareBlur",
                                                       "FlareComposite", "Output"});
    REQUIRE(Samples(RendererTest::Labelled(passes, "FlareGhosts").at(0),
                    RendererTest::Labelled(passes, "BloomDownsample").at(1).pass.colour.at(0).texture));
    INFO("an intensity over 1 mixes in the flare alone, and the ghost count is held to MaxGhosts");
    REQUIRE(Near(BloomBlockOf(test, RendererTest::Labelled(passes, "FlareComposite").at(0)).blend, 1.0f));
    REQUIRE(FlareBlockOf(test, RendererTest::Labelled(passes, "FlareGhosts").at(0)).ghostCount == MaxGhosts);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Lens dirt makes bloom's and the flare's composites read the dirt image", "[renderer][flare]")
{
    RendererTestApp test;
    Fenrir::Camera& camera = AddFlaringCamera(test, 0.05f);
    camera.lensFlare.dirt = AddDirt(test, 200);
    camera.lensFlare.dirtIntensity = 2.5f;

    test.Frame();
    const std::vector<PassRecord> passes = test.Passes();
    const PassRecord bloom = RendererTest::Labelled(passes, "BloomComposite").at(0);
    const PassRecord flare = RendererTest::Labelled(passes, "FlareComposite").at(0);
    INFO("both composites take the DIRT variant, reading their level and the dirt, with the dirt intensity");
    for (const PassRecord& composite : {bloom, flare})
    {
        REQUIRE(RendererProbe::Keys(test.Device(), composite.pipeline) == std::vector<std::string>{"DIRT"});
        REQUIRE(SampledCount(composite) == 2);
        REQUIRE(Near(BloomBlockOf(test, composite).dirtIntensity, 2.5f));
        REQUIRE(RendererProbe::State(test.Device(), composite.pipeline).blend == BlendMode::Alpha);
    }
    INFO("one dirt texture, read by both");
    // What each composite samples besides its own level.
    const auto otherThan = [](const PassRecord& pass, TextureHandle level) {
        for (const auto& use : pass.pass.uses)
        {
            if (use.usage == ResourceUsage::Sampled && use.texture != level)
            {
                return use.texture;
            }
        }
        return TextureHandle{};
    };
    const TextureHandle bloomLevel0 = RendererTest::Labelled(passes, "BloomDownsample").at(0).pass.colour.at(0).texture;
    const TextureHandle blurred = RendererTest::Labelled(passes, "FlareBlur").at(0).pass.colour.at(0).texture;
    REQUIRE(Samples(bloom, bloomLevel0));
    REQUIRE(Samples(flare, blurred));
    REQUIRE(!otherThan(bloom, bloomLevel0).IsEmpty());
    REQUIRE(otherThan(bloom, bloomLevel0) == otherThan(flare, blurred));
    INFO("the blur reads no dirt");
    REQUIRE(RendererProbe::Keys(test.Device(), RendererTest::Labelled(passes, "FlareBlur").at(0).pipeline).empty());
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Lens dirt with the flare off scales bloom alone; with no intensity it is none", "[renderer][flare]")
{
    RendererTestApp test;
    Fenrir::Camera& camera = AddFlaringCamera(test, 0.0f);
    camera.lensFlare.dirt = AddDirt(test, 255);

    test.Frame();
    std::vector<std::string> labels = Labels(test.Passes());
    INFO("no flare pass, and bloom's composite reads the dirt");
    REQUIRE(std::none_of(labels.begin(), labels.end(),
                         [](const std::string& label) { return label.rfind("Flare", 0) == 0; }));
    REQUIRE(
        RendererProbe::Keys(test.Device(), RendererTest::Labelled(test.Passes(), "BloomComposite").at(0).pipeline) ==
        std::vector<std::string>{"DIRT"});

    camera.lensFlare.dirtIntensity = 0.0f;
    test.Frame();
    INFO("a dirt intensity of 0 is the plain composite");
    REQUIRE(RendererProbe::Keys(test.Device(), RendererTest::Labelled(test.Passes(), "BloomComposite").at(0).pipeline)
                .empty());
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Lens dirt that is not a flat image with pixels is none, logged once", "[renderer][flare]")
{
    RendererTestApp test;
    Fenrir::Camera& camera = AddFlaringCamera(test, 0.05f);
    camera.lensFlare.dirt = test.TargetImage(16, 16);

    test.Frame();
    test.Frame();
    INFO("a render target is no lens dirt: the composites are the plain ones");
    REQUIRE(RendererProbe::Keys(test.Device(), RendererTest::Labelled(test.Passes(), "FlareComposite").at(0).pipeline)
                .empty());
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "lens dirt") == 1);
}
