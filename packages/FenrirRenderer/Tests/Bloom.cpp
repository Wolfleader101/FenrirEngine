// Bloom (ADR-0049): how many levels a view's chain has, what each adds and what each view's plan runs, on the CPU;
// and on the null backend (the spec's seam 1), each view's BloomDownsample, BloomUpsample and BloomComposite passes in
// PostProcess, their blocks, blending and samples, and none at intensity 0.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/Bloom.hpp"
#include "RendererTestApp.hpp"

using Fenrir::RendererProbe;
using Fenrir::Graphics::BlendMode;
using Fenrir::Graphics::LoadOp;
using Fenrir::Graphics::PixelRect;
using Fenrir::Graphics::ResourceUsage;
using Fenrir::Graphics::TextureHandle;
using Fenrir::Render::BloomLevels;
using Fenrir::Render::BloomLevelWeights;
using Fenrir::Render::BloomPipelines;
using Fenrir::Render::BloomPlan;
using Fenrir::Render::BloomView;
using Fenrir::Render::PlanBloom;
using RendererTest::PassRecord;
using RendererTest::RendererTestApp;

namespace
{
    /** @brief What a bloom pass's block at set 1 holds, as builtin:bloom-downsample and -upsample declare it. */
    struct BloomBlock
    {
        float texelX = 0.0f;
        float texelY = 0.0f;
        float blend = 0.0f;
        std::uint32_t karis = 0;
    };

    BloomBlock BlockOf(const RendererTestApp& test, const PassRecord& pass)
    {
        const auto entries = RendererProbe::Entries(test.Device(), pass.groups.at(1));
        REQUIRE(entries.size() == 1);
        const auto bytes = RendererProbe::Bytes(test.Device(), entries[0].buffer);
        const std::size_t at = entries[0].offset;
        return {RendererTest::FloatAt(bytes, at), RendererTest::FloatAt(bytes, at + 4),
                RendererTest::FloatAt(bytes, at + 8), RendererTest::UintAt(bytes, at + 12)};
    }

    /** @brief Whether `pass` samples `texture`. */
    bool Samples(const PassRecord& pass, TextureHandle texture)
    {
        return std::any_of(pass.pass.uses.begin(), pass.pass.uses.end(), [&](const auto& use) {
            return use.texture == texture && use.usage == ResourceUsage::Sampled;
        });
    }

    /** @brief The labels of `passes`, in order. */
    std::vector<std::string> Labels(const std::vector<PassRecord>& passes)
    {
        std::vector<std::string> labels;
        for (const PassRecord& pass : passes)
        {
            labels.push_back(pass.pass.label);
        }
        return labels;
    }

    /** @brief A view's passes from DepthPrepass on, as a camera with its bloom passes in a 1280 x 720 window has them.
     */
    std::vector<std::string> ViewPasses(std::uint32_t levels)
    {
        std::vector<std::string> labels{"DepthPrepass", "Opaque", "Background"};
        labels.insert(labels.end(), levels, "BloomDownsample");
        labels.insert(labels.end(), levels - 1, "BloomUpsample");
        labels.emplace_back("BloomComposite");
        labels.emplace_back("Output");
        labels.emplace_back("CullReadback");
        return labels;
    }

    bool Near(float a, float b, float tolerance = 1e-6f)
    {
        return std::abs(a - b) < tolerance;
    }
    /**
     * @brief A 256 x 128 view to plan, at these intensities: six bloom levels (128 down to 4 on the shorter side),
     * seven streak levels (level 0's 128 wide down to 2), and the flare at level 1.
     */
    BloomView PlanView(float bloom, float streaks, float flare)
    {
        BloomView view;
        view.size = {256, 128};
        view.settings.intensity = bloom;
        view.anamorphicStreaks.intensity = streaks;
        view.lensFlare.intensity = flare;
        return view;
    }

    /** @brief `view` with lens dirt imported, at the default dirt intensity. */
    BloomView Dirty(BloomView view)
    {
        view.dirt.id = 3;
        return view;
    }
} // namespace

TEST_CASE("A bloom chain has a level per halving of the view's shorter side, at most six", "[renderer][bloom]")
{
    INFO("a 1280 x 720 view halves six times before 720 runs out, the most it may");
    REQUIRE(BloomLevels({1280, 720}) == 6);
    REQUIRE(BloomLevels({64, 64}) == 6);
    INFO("a small view has fewer: 8 x 8 halves to 4, 2 and 1");
    REQUIRE(BloomLevels({8, 8}) == 3);
    REQUIRE(BloomLevels({2, 300}) == 1);
    INFO("a view a pixel wide or tall has none");
    REQUIRE(BloomLevels({1, 300}) == 0);
    REQUIRE(BloomLevels({0, 0}) == 0);
}

TEST_CASE("The bloom levels' weights fall by the radius and sum to one", "[renderer][bloom]")
{
    INFO("radius 0.5 over three levels: half for the first, a quarter for the second, the rest for the widest");
    REQUIRE(BloomLevelWeights(0.5f, 3) == std::vector<float>{0.5f, 0.25f, 0.25f});
    INFO("radius 0 keeps the narrowest only, radius 1 the widest only");
    REQUIRE(BloomLevelWeights(0.0f, 3) == std::vector<float>{1.0f, 0.0f, 0.0f});
    REQUIRE(BloomLevelWeights(1.0f, 3) == std::vector<float>{0.0f, 0.0f, 1.0f});
    INFO("the default radius over six levels sums to one, so the bloom keeps the scene's energy");
    const std::vector<float> weights = BloomLevelWeights(0.7f, 6);
    REQUIRE(weights.size() == 6);
    REQUIRE(Near(std::accumulate(weights.begin(), weights.end(), 0.0f), 1.0f, 1e-5f));
    INFO("a radius outside [0, 1] is clamped");
    REQUIRE(BloomLevelWeights(2.0f, 2) == std::vector<float>{0.0f, 1.0f});
}

TEST_CASE("A view's bloom plan with nothing on runs nothing", "[renderer][bloom]")
{
    const BloomPlan plan = PlanBloom(PlanView(0.0f, 0.0f, 0.0f), BloomPipelines{});
    REQUIRE_FALSE(plan.Runs());
    REQUIRE(plan.levels == 0);
    REQUIRE(plan.blocks == 0);
    INFO("nor does a view under 2 pixels on a side, whatever it turns on");
    BloomView tiny = PlanView(1.0f, 1.0f, 1.0f);
    tiny.size = {1, 1};
    REQUIRE_FALSE(PlanBloom(tiny, BloomPipelines{}).Runs());
}

TEST_CASE("A view's bloom plan runs each part alone with a block for each of its passes", "[renderer][bloom]")
{
    {
        INFO("bloom alone: six downsamples, five upsamples and its composite");
        const BloomPlan plan = PlanBloom(PlanView(0.04f, 0.0f, 0.0f), BloomPipelines{});
        REQUIRE(plan.bloom);
        REQUIRE_FALSE(plan.streaks);
        REQUIRE_FALSE(plan.flare);
        REQUIRE(plan.levels == 6);
        REQUIRE(plan.streakLevels == 0);
        REQUIRE(plan.blocks == 12);
        REQUIRE(plan.lensDirtBlocks == 0);
    }
    {
        INFO("the streaks without bloom: bloom's first downsample, then seven streak levels down and back up");
        const BloomPlan plan = PlanBloom(PlanView(0.0f, 0.1f, 0.0f), BloomPipelines{});
        REQUIRE_FALSE(plan.bloom);
        REQUIRE(plan.streaks);
        REQUIRE(plan.levels == 1);
        REQUIRE(plan.streakLevels == 7);
        REQUIRE(plan.blocks == 1 + 14);
    }
    {
        INFO("the flare without bloom: bloom's downsamples down to level 1, its ghosts, blur and composite");
        const BloomPlan plan = PlanBloom(PlanView(0.0f, 0.0f, 0.03f), BloomPipelines{});
        REQUIRE(plan.flare);
        REQUIRE(plan.levels == 2);
        REQUIRE(plan.blocks == 2 + 3);
    }
}

TEST_CASE("Lens dirt moves the composites that read it to the lens dirt ring, never the streaks'", "[renderer][bloom]")
{
    {
        INFO("bloom alone under dirt: its composite takes a lens dirt block");
        const BloomPlan plan = PlanBloom(Dirty(PlanView(0.04f, 0.0f, 0.0f)), BloomPipelines{});
        REQUIRE(plan.lensDirt);
        REQUIRE(plan.blocks == 11);
        REQUIRE(plan.lensDirtBlocks == 1);
    }
    {
        INFO("the flare alone under dirt: its ghosts and blur stay plain, its composite reads the dirt");
        const BloomPlan plan = PlanBloom(Dirty(PlanView(0.0f, 0.0f, 0.03f)), BloomPipelines{});
        REQUIRE(plan.lensDirt);
        REQUIRE(plan.blocks == 2 + 2);
        REQUIRE(plan.lensDirtBlocks == 1);
    }
    {
        INFO("every part under dirt: bloom's and the flare's composites, not the streaks'");
        const BloomPlan plan = PlanBloom(Dirty(PlanView(0.04f, 0.1f, 0.03f)), BloomPipelines{});
        REQUIRE(plan.blocks == 6 + 5 + 14 + 2);
        REQUIRE(plan.lensDirtBlocks == 2);
    }
    {
        INFO("the streaks alone have no composite to read it");
        REQUIRE_FALSE(PlanBloom(Dirty(PlanView(0.0f, 0.1f, 0.0f)), BloomPipelines{}).lensDirt);
    }
    {
        INFO("a dirt intensity of 0 reads none");
        BloomView view = Dirty(PlanView(0.04f, 0.0f, 0.0f));
        view.lensFlare.dirtIntensity = 0.0f;
        REQUIRE_FALSE(PlanBloom(view, BloomPipelines{}).lensDirt);
    }
}

TEST_CASE("A part whose pipelines the bloom chain could not make never runs", "[renderer][bloom]")
{
    // Dirt without its pipeline falls back to the plain composite, so bloom keeps all its blocks in the plain ring.
    const BloomPlan plan = PlanBloom(Dirty(PlanView(0.04f, 0.1f, 0.03f)), BloomPipelines{false, false, false});
    REQUIRE(plan.bloom);
    REQUIRE_FALSE(plan.streaks);
    REQUIRE_FALSE(plan.flare);
    REQUIRE_FALSE(plan.lensDirt);
    REQUIRE(plan.blocks == 12);
    REQUIRE(plan.lensDirtBlocks == 0);
}

TEST_CASE("A camera's bloom runs in PostProcess, down the chain and back up into SceneColor", "[renderer][bloom]")
{
    RendererTestApp test;
    test.AddCamera();
    test.AddMeshRenderer(test.Mesh(), test.Material());

    test.Frame();
    const std::vector<PassRecord> passes = test.Passes();
    INFO("after the scene and before Output: six downsamples, five upsamples, and the composite");
    std::vector<std::string> expected{"Cull", "BrdfLut"};
    const std::vector<std::string> view = ViewPasses(6);
    expected.insert(expected.end(), view.begin(), view.end());
    REQUIRE(Labels(passes) == RendererTest::WithOcclusion(expected));

    const TextureHandle sceneColour = RendererTest::Labelled(passes, "Opaque").at(0).pass.colour.at(0).texture;
    const std::vector<PassRecord> down = RendererTest::Labelled(passes, "BloomDownsample");
    const std::vector<PassRecord> up = RendererTest::Labelled(passes, "BloomUpsample");
    const PassRecord composite = RendererTest::Labelled(passes, "BloomComposite").at(0);

    INFO("the first downsample reads SceneColor into level 0 at half its size, taking the Karis average");
    REQUIRE(Samples(down[0], sceneColour));
    REQUIRE(down[0].viewport == PixelRect{0, 0, 640, 360});
    REQUIRE(BlockOf(test, down[0]).karis == 1);
    REQUIRE(Near(BlockOf(test, down[0]).texelX, 1.0f / 1280.0f));
    REQUIRE(Near(BlockOf(test, down[0]).texelY, 1.0f / 720.0f));
    INFO("each later one reads the level before into the next at half again, without it");
    const std::vector<PixelRect> sizes{{0, 0, 640, 360}, {0, 0, 320, 180}, {0, 0, 160, 90},
                                       {0, 0, 80, 45},   {0, 0, 40, 22},   {0, 0, 20, 11}};
    for (std::size_t level = 1; level < down.size(); ++level)
    {
        REQUIRE(Samples(down[level], down[level - 1].pass.colour.at(0).texture));
        REQUIRE(down[level].viewport == sizes[level]);
        REQUIRE(BlockOf(test, down[level]).karis == 0);
        REQUIRE(Near(BlockOf(test, down[level]).texelX, 1.0f / static_cast<float>(sizes[level - 1].width)));
    }
    INFO("the downsamples write their levels whole, unblended");
    REQUIRE(down[0].pass.colour.at(0).load == LoadOp::DontCare);
    REQUIRE(RendererProbe::State(test.Device(), down[0].pipeline).blend == BlendMode::None);

    INFO("each upsample reads the level below and blends over its own by the radius, 0.7 by default");
    for (std::size_t i = 0; i < up.size(); ++i)
    {
        const std::size_t level = up.size() - 1 - i;
        REQUIRE(Samples(up[i], down[level + 1].pass.colour.at(0).texture));
        REQUIRE(up[i].pass.colour.at(0).texture == down[level].pass.colour.at(0).texture);
        REQUIRE(up[i].pass.colour.at(0).load == LoadOp::Load);
        REQUIRE(up[i].viewport == sizes[level]);
        REQUIRE(Near(BlockOf(test, up[i]).blend, 0.7f));
    }
    REQUIRE(RendererProbe::State(test.Device(), up[0].pipeline).blend == BlendMode::Alpha);

    INFO("the composite blends level 0 over the whole of SceneColor by the intensity, 0.04 by default");
    REQUIRE(Samples(composite, down[0].pass.colour.at(0).texture));
    REQUIRE(composite.pass.colour.at(0).texture == sceneColour);
    REQUIRE(composite.pass.colour.at(0).load == LoadOp::Load);
    REQUIRE(composite.viewport == PixelRect{0, 0, 1280, 720});
    REQUIRE(Near(BlockOf(test, composite).blend, 0.04f));
    REQUIRE(RendererProbe::State(test.Device(), composite.pipeline).blend == BlendMode::Alpha);
    INFO("the levels are single-sampled RGBA16F");
    REQUIRE(RendererProbe::Desc(test.Device(), down[0].pass.colour.at(0).texture).format ==
            Fenrir::Graphics::TextureFormat::Rgba16Float);
    REQUIRE(test.messages->errors.empty());
}

namespace
{
    /** @brief Two cameras side by side, each with half the 1280 x 720 window; the right one drawn second. */
    Fenrir::Camera& AddTwoCameras(RendererTestApp& test)
    {
        test.AddCamera().GetComponent<Fenrir::Camera>().viewport = {0.0f, 0.0f, 0.5f, 1.0f};
        Fenrir::Camera& right = test.AddCamera().GetComponent<Fenrir::Camera>();
        right.viewport = {0.5f, 0.0f, 0.5f, 1.0f};
        right.order = 1;
        test.AddMeshRenderer(test.Mesh(), test.Material());
        return right;
    }
} // namespace

TEST_CASE("Each view blooms its own SceneColor", "[renderer][bloom]")
{
    RendererTestApp test;
    AddTwoCameras(test);

    test.Frame();
    const std::vector<PassRecord> passes = test.Passes();
    const std::vector<PassRecord> opaque = RendererTest::Labelled(passes, "Opaque");
    const std::vector<PassRecord> down = RendererTest::Labelled(passes, "BloomDownsample");
    const std::vector<PassRecord> composites = RendererTest::Labelled(passes, "BloomComposite");
    INFO("two views, two chains of six levels, each from and into its own SceneColor");
    REQUIRE(opaque.size() == 2);
    REQUIRE(down.size() == 12);
    REQUIRE(RendererTest::Labelled(passes, "BloomUpsample").size() == 10);
    REQUIRE(composites.size() == 2);
    for (std::size_t v = 0; v < 2; ++v)
    {
        const TextureHandle sceneColour = opaque[v].pass.colour.at(0).texture;
        REQUIRE(Samples(down[v * 6], sceneColour));
        REQUIRE(composites[v].pass.colour.at(0).texture == sceneColour);
        REQUIRE(composites[v].viewport == PixelRect{0, 0, 640, 720});
    }
}

TEST_CASE("A camera at bloom intensity 0 loses its bloom passes while another camera keeps its own",
          "[renderer][bloom]")
{
    RendererTestApp test;
    AddTwoCameras(test).look.bloom.intensity = 0.0f;

    test.Frame();
    const std::vector<PassRecord> passes = test.Passes();
    const std::vector<PassRecord> opaque = RendererTest::Labelled(passes, "Opaque");
    const std::vector<PassRecord> down = RendererTest::Labelled(passes, "BloomDownsample");
    const std::vector<PassRecord> composites = RendererTest::Labelled(passes, "BloomComposite");
    INFO("the right view's passes are culled; the left view keeps its own");
    REQUIRE(down.size() == 6);
    REQUIRE(composites.size() == 1);
    REQUIRE(composites[0].pass.colour.at(0).texture == opaque[0].pass.colour.at(0).texture);
}

TEST_CASE("A camera at bloom intensity 0 draws the frame as it was without bloom", "[renderer][bloom]")
{
    RendererTestApp test;
    test.AddCamera().GetComponent<Fenrir::Camera>().look.bloom.intensity = 0.0f;
    test.AddMeshRenderer(test.Mesh(), test.Material());

    test.Frame();
    INFO("no bloom pass between Background and Output");
    REQUIRE(Labels(test.Passes()) == RendererTest::WithOcclusion({"Cull", "BrdfLut", "DepthPrepass", "Opaque",
                                                                  "Background", "Output", "CullReadback"}));
}

TEST_CASE("A camera's bloom intensity and radius reach its passes, clamped to [0, 1]", "[renderer][bloom]")
{
    RendererTestApp test;
    Fenrir::Camera& camera = test.AddCamera().GetComponent<Fenrir::Camera>();
    camera.look.bloom.intensity = 0.25f;
    camera.look.bloom.radius = 0.4f;
    test.Frame();
    INFO("the composite blends by the intensity, each upsample by the radius");
    REQUIRE(Near(BlockOf(test, RendererTest::Labelled(test.Passes(), "BloomComposite").at(0)).blend, 0.25f));
    REQUIRE(Near(BlockOf(test, RendererTest::Labelled(test.Passes(), "BloomUpsample").at(0)).blend, 0.4f));

    camera.look.bloom.intensity = 3.0f;
    camera.look.bloom.radius = -1.0f;
    test.Frame();
    INFO("an intensity over 1 mixes in the bloom alone; a radius under 0 keeps each level to itself");
    REQUIRE(Near(BlockOf(test, RendererTest::Labelled(test.Passes(), "BloomComposite").at(0)).blend, 1.0f));
    REQUIRE(Near(BlockOf(test, RendererTest::Labelled(test.Passes(), "BloomUpsample").at(0)).blend, 0.0f));
}

TEST_CASE("Under MSAA the first downsample resolves SceneColor and the composite draws every sample",
          "[renderer][bloom][msaa]")
{
    RendererTestApp test([] {
        Fenrir::RendererConfig config = RendererTest::NullBackend();
        config.msaa = 4;
        return config;
    }());
    test.AddCamera();
    test.AddMeshRenderer(test.Mesh(), test.Material());

    test.Frame();
    const std::vector<PassRecord> down = RendererTest::Labelled(test.Passes(), "BloomDownsample");
    const PassRecord composite = RendererTest::Labelled(test.Passes(), "BloomComposite").at(0);
    INFO("the first downsample runs the MSAA variant into a single-sampled level; the rest the plain one");
    REQUIRE(RendererProbe::Keys(test.Device(), down.at(0).pipeline) == std::vector<std::string>{"MSAA"});
    REQUIRE(RendererProbe::Desc(test.Device(), down.at(0).pass.colour.at(0).texture).samples == 1);
    REQUIRE(RendererProbe::Keys(test.Device(), down.at(1).pipeline).empty());
    INFO("the composite's pipeline draws at SceneColor's four samples");
    REQUIRE(RendererProbe::Samples(test.Device(), composite.pipeline) == 4);
    REQUIRE(RendererProbe::Desc(test.Device(), composite.pass.colour.at(0).texture).samples == 4);
    REQUIRE(test.messages->errors.empty());
}
