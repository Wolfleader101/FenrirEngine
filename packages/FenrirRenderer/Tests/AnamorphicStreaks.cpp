// Anamorphic streaks (ADR-0059): how many levels a view's streak chain has, what each adds and how the tint is
// normalised, on the CPU; and on the null backend (the spec's seam 1), each view's StreakDownsample, StreakUpsample and
// StreakComposite passes in PostProcess beside bloom's, their blocks and blending, none while the streaks are off, and
// the downsample of their own they run when bloom is off.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/Bloom.hpp"
#include "Core/ColourGrading.hpp"
#include "RendererTestApp.hpp"

using Fenrir::RendererProbe;
using Fenrir::Graphics::BlendMode;
using Fenrir::Graphics::LoadOp;
using Fenrir::Graphics::PixelRect;
using Fenrir::Graphics::ResourceUsage;
using Fenrir::Graphics::TextureHandle;
using Fenrir::Math::Vec3;
using Fenrir::Render::StreakLevels;
using Fenrir::Render::StreakLevelWeights;
using Fenrir::Render::UnitLuminanceTint;
using RendererTest::PassRecord;
using RendererTest::RendererTestApp;

namespace
{
    /** @brief What a bloom or streak pass's block at set 1 holds, as builtin:bloom-downsample and -upsample declare it.
     */
    struct BloomBlock
    {
        float texelX = 0.0f;
        float texelY = 0.0f;
        float blend = 0.0f;
        std::uint32_t karis = 0;
        Vec3 tint{0.0f};
    };

    BloomBlock BlockOf(const RendererTestApp& test, const PassRecord& pass)
    {
        const auto entries = RendererProbe::Entries(test.Device(), pass.groups.at(1));
        REQUIRE(entries.size() == 1);
        const auto bytes = RendererProbe::Bytes(test.Device(), entries[0].buffer);
        const std::size_t at = entries[0].offset;
        return {RendererTest::FloatAt(bytes, at),
                RendererTest::FloatAt(bytes, at + 4),
                RendererTest::FloatAt(bytes, at + 8),
                RendererTest::UintAt(bytes, at + 12),
                {RendererTest::FloatAt(bytes, at + 16), RendererTest::FloatAt(bytes, at + 20),
                 RendererTest::FloatAt(bytes, at + 24)}};
    }

    bool Samples(const PassRecord& pass, TextureHandle texture)
    {
        return std::any_of(pass.pass.uses.begin(), pass.pass.uses.end(), [&](const auto& use) {
            return use.texture == texture && use.usage == ResourceUsage::Sampled;
        });
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

    bool Near(float a, float b, float tolerance = 1e-6f)
    {
        return std::abs(a - b) < tolerance;
    }

    float Luminance(Vec3 colour)
    {
        return 0.2126f * colour.r + 0.7152f * colour.g + 0.0722f * colour.b;
    }

    /** @brief A camera with a mesh in front of it, its streaks on at `intensity`. */
    Fenrir::Camera& AddStreakingCamera(RendererTestApp& test, float intensity)
    {
        Fenrir::Camera& camera = test.AddCamera().GetComponent<Fenrir::Camera>();
        camera.anamorphicStreaks.intensity = intensity;
        test.AddMeshRenderer(test.Mesh(), test.Material());
        return camera;
    }
} // namespace

TEST_CASE("A streak chain has a level per halving of bloom's first level's width, at most eight", "[renderer][streaks]")
{
    INFO("a 1280 x 720 view's bloom level 0 is 640 wide: 320, 160, 80, 40, 20, 10, 5 and 2, the most it may");
    REQUIRE(StreakLevels({1280, 720}) == 8);
    INFO("a 64 x 64 view's level 0 is 32 wide: 16, 8, 4, 2 and 1");
    REQUIRE(StreakLevels({64, 64}) == 5);
    INFO("the height does not halve, so a wide, short view has as many as a square one");
    REQUIRE(StreakLevels({64, 2}) == 5);
    INFO("a view whose level 0 is a pixel wide, or that has no level 0, has none");
    REQUIRE(StreakLevels({3, 300}) == 0);
    REQUIRE(StreakLevels({300, 1}) == 0);
    REQUIRE(StreakLevels({0, 0}) == 0);
}

TEST_CASE("The streak levels' weights fall by the length and sum to one for every length", "[renderer][streaks]")
{
    INFO("length 0.5 over three levels: half for the narrowest, a quarter for the next, the rest for the longest");
    REQUIRE(StreakLevelWeights(0.5f, 3) == std::vector<float>{0.5f, 0.25f, 0.25f});
    INFO("length 0 keeps the narrowest only, length 1 the longest only, and outside [0, 1] it is clamped");
    REQUIRE(StreakLevelWeights(0.0f, 3) == std::vector<float>{1.0f, 0.0f, 0.0f});
    REQUIRE(StreakLevelWeights(1.0f, 3) == std::vector<float>{0.0f, 0.0f, 1.0f});
    REQUIRE(StreakLevelWeights(-1.0f, 2) == std::vector<float>{1.0f, 0.0f});
    for (const float length : {0.0f, 0.1f, 0.35f, 0.5f, 0.75f, 0.9f, 1.0f})
    {
        const std::vector<float> weights = StreakLevelWeights(length, 8);
        INFO("length " << length);
        REQUIRE(weights.size() == 8);
        REQUIRE(Near(std::accumulate(weights.begin(), weights.end(), 0.0f), 1.0f, 1e-5f));
    }
}

TEST_CASE("A tint normalised for the streaks has unit luminance and keeps its hue", "[renderer][streaks]")
{
    for (const Vec3 tint : {Vec3(0.2f, 0.4f, 1.0f), Vec3(3.0f, 0.5f, 0.1f), Vec3(0.0f, 0.0f, 0.02f)})
    {
        const Vec3 normalised = UnitLuminanceTint(tint);
        INFO("tint (" << tint.r << ", " << tint.g << ", " << tint.b << ")");
        REQUIRE(Near(Luminance(normalised), 1.0f, 1e-5f));
        REQUIRE(Near(normalised.r * tint.b, normalised.b * tint.r, 1e-5f));
        REQUIRE(Near(normalised.g * tint.b, normalised.b * tint.g, 1e-5f));
    }
    INFO("grey of any brightness is white, and black, with no luminance to scale, is white too");
    REQUIRE(UnitLuminanceTint(Vec3(0.3f)) == Vec3(1.0f));
    REQUIRE(UnitLuminanceTint(Vec3(0.0f)) == Vec3(1.0f));
    INFO("negatives are clamped first");
    REQUIRE(Near(UnitLuminanceTint(Vec3(-1.0f, 0.0f, 1.0f)).b, 1.0f / 0.0722f, 1e-3f));
}

TEST_CASE("A camera's streaks are off by default and add no pass", "[renderer][streaks]")
{
    RendererTestApp test;
    test.AddCamera();
    test.AddMeshRenderer(test.Mesh(), test.Material());
    INFO("a camera's streak intensity is 0 by default");
    REQUIRE(Fenrir::Camera{}.anamorphicStreaks.intensity == 0.0f);

    test.Frame();
    const std::vector<std::string> labels = Labels(test.Passes());
    INFO("bloom's passes only, between Background and Output");
    REQUIRE(std::none_of(labels.begin(), labels.end(),
                         [](const std::string& label) { return label.rfind("Streak", 0) == 0; }));
    REQUIRE(RendererTest::Labelled(test.Passes(), "BloomDownsample").size() == 6);
}

TEST_CASE("A camera's streaks run in PostProcess beside its bloom, from bloom's first level back into SceneColor",
          "[renderer][streaks]")
{
    RendererTestApp test;
    Fenrir::Camera& camera = AddStreakingCamera(test, 0.1f);
    camera.anamorphicStreaks.length = 0.6f;

    test.Frame();
    const std::vector<PassRecord> passes = test.Passes();
    INFO("the streaks' downsamples read bloom's level 0 before bloom's upsamples blend over it; the rest of their "
         "passes follow bloom's composite");
    std::vector<std::string> expected{"BrdfLut", "DepthPrepass", "Opaque", "Background"};
    expected.insert(expected.end(), 6, "BloomDownsample");
    expected.insert(expected.end(), 8, "StreakDownsample");
    expected.insert(expected.end(), 5, "BloomUpsample");
    expected.emplace_back("BloomComposite");
    expected.insert(expected.end(), 7, "StreakUpsample");
    expected.emplace_back("StreakComposite");
    expected.emplace_back("Output");
    REQUIRE(Labels(passes) == expected);

    const TextureHandle sceneColour = RendererTest::Labelled(passes, "Opaque").at(0).pass.colour.at(0).texture;
    const TextureHandle bloomLevel0 = RendererTest::Labelled(passes, "BloomDownsample").at(0).pass.colour.at(0).texture;
    const std::vector<PassRecord> down = RendererTest::Labelled(passes, "StreakDownsample");
    const std::vector<PassRecord> up = RendererTest::Labelled(passes, "StreakUpsample");
    const PassRecord composite = RendererTest::Labelled(passes, "StreakComposite").at(0);

    INFO("the first reads bloom's level 0, 640 x 360, into 320 x 360: only the width halves");
    REQUIRE(Samples(down[0], bloomLevel0));
    REQUIRE(down[0].viewport == PixelRect{0, 0, 320, 360});
    REQUIRE(Near(BlockOf(test, down[0]).texelX, 1.0f / 640.0f));
    REQUIRE(Near(BlockOf(test, down[0]).texelY, 1.0f / 360.0f));
    REQUIRE(BlockOf(test, down[0]).karis == 0);
    INFO("each later one halves the width again, the height staying 360, with the horizontal variant");
    const std::vector<std::uint32_t> widths{320, 160, 80, 40, 20, 10, 5, 2};
    for (std::size_t level = 0; level < down.size(); ++level)
    {
        if (level > 0)
        {
            REQUIRE(Samples(down[level], down[level - 1].pass.colour.at(0).texture));
        }
        REQUIRE(down[level].viewport == PixelRect{0, 0, widths[level], 360});
        REQUIRE(down[level].pass.colour.at(0).load == LoadOp::DontCare);
        REQUIRE(RendererProbe::Keys(test.Device(), down[level].pipeline) == std::vector<std::string>{"HORIZONTAL"});
    }
    REQUIRE(RendererProbe::State(test.Device(), down[0].pipeline).blend == BlendMode::None);

    INFO("each upsample reads the level below and blends over its own by the length, untinted");
    for (std::size_t i = 0; i < up.size(); ++i)
    {
        const std::size_t level = up.size() - 1 - i;
        REQUIRE(Samples(up[i], down[level + 1].pass.colour.at(0).texture));
        REQUIRE(up[i].pass.colour.at(0).texture == down[level].pass.colour.at(0).texture);
        REQUIRE(up[i].pass.colour.at(0).load == LoadOp::Load);
        REQUIRE(up[i].viewport == PixelRect{0, 0, widths[level], 360});
        REQUIRE(Near(BlockOf(test, up[i]).blend, 0.6f));
        REQUIRE(BlockOf(test, up[i]).tint == Vec3(1.0f));
        REQUIRE(RendererProbe::Keys(test.Device(), up[i].pipeline) == std::vector<std::string>{"HORIZONTAL"});
    }
    REQUIRE(RendererProbe::State(test.Device(), up[0].pipeline).blend == BlendMode::Alpha);

    INFO("the composite blends the first streak level over the whole of SceneColor by the intensity");
    REQUIRE(Samples(composite, down[0].pass.colour.at(0).texture));
    REQUIRE(composite.pass.colour.at(0).texture == sceneColour);
    REQUIRE(composite.pass.colour.at(0).load == LoadOp::Load);
    REQUIRE(composite.viewport == PixelRect{0, 0, 1280, 720});
    REQUIRE(Near(BlockOf(test, composite).blend, 0.1f));
    REQUIRE(RendererProbe::State(test.Device(), composite.pipeline).blend == BlendMode::Alpha);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("The streak composite carries the camera's tint at unit luminance, and its intensity clamped",
          "[renderer][streaks]")
{
    RendererTestApp test;
    Fenrir::Camera& camera = AddStreakingCamera(test, 3.0f);
    camera.anamorphicStreaks.tint = Fenrir::Colour{0.2f, 0.4f, 1.0f, 1.0f};

    test.Frame();
    const BloomBlock block = BlockOf(test, RendererTest::Labelled(test.Passes(), "StreakComposite").at(0));
    INFO("an intensity over 1 mixes in the streaks alone");
    REQUIRE(Near(block.blend, 1.0f));
    INFO("the tint keeps its hue at a luminance of 1, so the streaks move light and add none");
    REQUIRE(Near(Luminance(block.tint), 1.0f, 1e-5f));
    REQUIRE(Near(block.tint.g, 2.0f * block.tint.r, 1e-4f));
    REQUIRE(Near(block.tint.b, 5.0f * block.tint.r, 1e-4f));
    INFO("bloom's own composite stays untinted");
    REQUIRE(BlockOf(test, RendererTest::Labelled(test.Passes(), "BloomComposite").at(0)).tint == Vec3(1.0f));
}

TEST_CASE("A camera with bloom off still streaks, running bloom's first downsample for itself", "[renderer][streaks]")
{
    RendererTestApp test;
    Fenrir::Camera& camera = AddStreakingCamera(test, 0.1f);
    camera.bloom.intensity = 0.0f;

    test.Frame();
    const std::vector<PassRecord> passes = test.Passes();
    std::vector<std::string> expected{"BrdfLut", "DepthPrepass", "Opaque", "Background", "BloomDownsample"};
    expected.insert(expected.end(), 8, "StreakDownsample");
    expected.insert(expected.end(), 7, "StreakUpsample");
    expected.emplace_back("StreakComposite");
    expected.emplace_back("Output");
    INFO("one downsample of SceneColor into a level 0, then the streaks' passes; no bloom upsample or composite");
    REQUIRE(Labels(passes) == expected);

    const PassRecord level0 = RendererTest::Labelled(passes, "BloomDownsample").at(0);
    const TextureHandle sceneColour = RendererTest::Labelled(passes, "Opaque").at(0).pass.colour.at(0).texture;
    REQUIRE(Samples(level0, sceneColour));
    REQUIRE(level0.viewport == PixelRect{0, 0, 640, 360});
    INFO("with the Karis average, as bloom's own first level has it");
    REQUIRE(BlockOf(test, level0).karis == 1);
    REQUIRE(Samples(RendererTest::Labelled(passes, "StreakDownsample").at(0), level0.pass.colour.at(0).texture));
    REQUIRE(RendererTest::Labelled(passes, "StreakComposite").at(0).pass.colour.at(0).texture == sceneColour);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Each view streaks by its own camera's settings", "[renderer][streaks]")
{
    RendererTestApp test;
    test.AddCamera().GetComponent<Fenrir::Camera>().viewport = {0.0f, 0.0f, 0.5f, 1.0f};
    Fenrir::Camera& right = test.AddCamera().GetComponent<Fenrir::Camera>();
    right.viewport = {0.5f, 0.0f, 0.5f, 1.0f};
    right.order = 1;
    right.anamorphicStreaks.intensity = 0.2f;
    test.AddMeshRenderer(test.Mesh(), test.Material());

    test.Frame();
    const std::vector<PassRecord> passes = test.Passes();
    const std::vector<PassRecord> composites = RendererTest::Labelled(passes, "StreakComposite");
    INFO("only the right view streaks, into its own SceneColor");
    REQUIRE(composites.size() == 1);
    REQUIRE(composites[0].pass.colour.at(0).texture ==
            RendererTest::Labelled(passes, "Opaque").at(1).pass.colour.at(0).texture);
    REQUIRE(composites[0].viewport == PixelRect{0, 0, 640, 720});
    REQUIRE(RendererTest::Labelled(passes, "BloomComposite").size() == 2);
}
