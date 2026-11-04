// Reflection inputs (ADR-0056, lighting ticket 29) on the null backend: the Hi-Z pyramid's level sizes and its
// reduction on the CPU; each view whose camera has reflections gets HiZ passes after GTAO's and SceneHistory passes
// before bloom, each drawing one mip of a texture kept across frames from the level above; a camera without them has
// neither; each camera keeps its own textures, two pyramids that swap each frame, freed when it goes and remade, with
// the history invalid for a frame, when its size changes.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/ImageUpload.hpp"
#include "Core/RenderGraph.hpp"
#include "Core/SceneHistory.hpp"
#include "RecordingLogger.hpp"
#include "RendererTestApp.hpp"

using Fenrir::RendererProbe;
using Fenrir::Graphics::BindingGroupEntry;
using Fenrir::Graphics::PixelRect;
using Fenrir::Graphics::PixelSize;
using Fenrir::Graphics::ResourceUsage;
using Fenrir::Graphics::TextureFormat;
using Fenrir::Graphics::TextureHandle;
using Fenrir::Render::MipCount;
using Fenrir::Render::MipSize;
using Fenrir::Render::ReduceHiZ;
using RendererTest::Labelled;
using RendererTest::PassRecord;
using RendererTest::RendererTestApp;

namespace
{
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

    /** @brief Where `label` first comes in `labels`; labels.size() when it does not. */
    std::size_t IndexOf(const std::vector<std::string>& labels, const std::string& label)
    {
        return static_cast<std::size_t>(std::find(labels.begin(), labels.end(), label) - labels.begin());
    }

    /** @brief Where `label` last comes in `labels`; labels.size() when it does not. */
    std::size_t LastIndexOf(const std::vector<std::string>& labels, const std::string& label)
    {
        const auto found = std::find(labels.rbegin(), labels.rend(), label);
        return found == labels.rend() ? labels.size() : static_cast<std::size_t>(labels.rend() - found - 1);
    }

    /** @brief Whether `pass` samples `texture`. */
    bool Samples(const PassRecord& pass, TextureHandle texture)
    {
        return std::any_of(pass.pass.uses.begin(), pass.pass.uses.end(), [&](const auto& use) {
            return use.texture == texture && use.usage == ResourceUsage::Sampled;
        });
    }

    /** @brief The texture a fullscreen pass bound at set 2 binding 0, its source. */
    BindingGroupEntry Source(const RendererTestApp& test, const PassRecord& pass)
    {
        const auto entries = RendererProbe::Entries(test.Device(), pass.groups.at(2));
        REQUIRE(entries.size() == 1);
        return entries[0];
    }

    /** @brief One camera over the 1280 x 720 window with reflections and nothing else of the post effects. */
    Fenrir::Camera& AddReflectingCamera(RendererTestApp& test)
    {
        Fenrir::Camera& camera = test.AddCamera().GetComponent<Fenrir::Camera>();
        camera.bloom.intensity = 0.0f;
        camera.reflections.enabled = true;
        test.AddMeshRenderer(test.Mesh(), test.Material());
        return camera;
    }

    /** @brief A camera's view at `size` with reflections, for SceneHistory alone. */
    Fenrir::Render::ViewRequest ReflectingView(std::uint32_t camera, PixelSize size)
    {
        Fenrir::Render::ViewRequest view;
        view.camera = camera;
        view.viewport = {0, 0, size.width, size.height};
        view.reflections.enabled = true;
        return view;
    }

    /** @brief SceneHistory on the null device, run one frame at a time without recording. */
    struct HistoryCore
    {
        std::shared_ptr<RendererTest::LoggedMessages> messages = std::make_shared<RendererTest::LoggedMessages>();
        RendererTest::RecordingLogger logger{messages};
        std::unique_ptr<Fenrir::Graphics::GraphicsDevice> device = Fenrir::Graphics::CreateNullDevice();
        Fenrir::Render::SceneHistory history{*device, logger, 1};

        /** @brief One frame with `view` alone, seen by `viewProjection`; its textures. */
        Fenrir::Render::HistoryView Frame(const Fenrir::Render::ViewRequest& view,
                                          const Fenrir::Math::Mat4& viewProjection = Fenrir::Math::Mat4(1.0f))
        {
            Fenrir::Render::RenderGraph graph;
            history.BeginFrame(std::span(&view, 1));
            Fenrir::Render::HistoryView frame =
                history.Import(graph, view, {view.viewport.width, view.viewport.height}, viewProjection);
            history.EndFrame();
            return frame;
        }
    };
} // namespace

TEST_CASE("A mip chain halves each level down to 1 x 1, rounding down, for even and odd views", "[renderer][ssr]")
{
    INFO("1280 x 720 has 11 levels, its longer side halving 10 times to 1");
    REQUIRE(MipCount(1280, 720) == 11);
    REQUIRE(MipSize({1280, 720}, 1) == PixelSize{640, 360});
    REQUIRE(MipSize({1280, 720}, 4) == PixelSize{80, 45});
    REQUIRE(MipSize({1280, 720}, 5) == PixelSize{40, 22});
    REQUIRE(MipSize({1280, 720}, 10) == PixelSize{1, 1});
    INFO("an odd view rounds each side down, and a side at 1 stays at 1");
    REQUIRE(MipCount(5, 3) == 3);
    REQUIRE(MipSize({5, 3}, 1) == PixelSize{2, 1});
    REQUIRE(MipSize({5, 3}, 2) == PixelSize{1, 1});
    INFO("a single pixel has one level");
    REQUIRE(MipCount(1, 1) == 1);
}

TEST_CASE("A Hi-Z level keeps the nearest depth of the texels it covers", "[renderer][ssr]")
{
    // 4 x 2, reversed-Z: larger is nearer.
    const std::vector<float> even{0.1f, 0.2f, 0.5f, 0.0f, 0.3f, 0.4f, 0.0f, 0.0f};
    INFO("each texel the largest of its 2 x 2");
    REQUIRE(ReduceHiZ(even, {4, 2}) == std::vector<float>{0.4f, 0.5f});
}

TEST_CASE("A Hi-Z level of an odd size folds the last row and column into its last texels", "[renderer][ssr]")
{
    // 5 x 3: the nearest depth is in the last column and the last row, which a plain 2 x 2 reduction leaves out.
    std::vector<float> odd(15, 0.1f);
    odd[2 * 5 + 4] = 0.9f;
    INFO("5 x 3 reduces to 2 x 1, its last texel covering three columns and all three rows");
    const std::vector<float> reduced = ReduceHiZ(odd, {5, 3});
    REQUIRE(reduced == std::vector<float>{0.1f, 0.9f});
    INFO("and 3 x 1 to 1 x 1, covering all three");
    REQUIRE(ReduceHiZ(std::vector<float>{0.1f, 0.2f, 0.7f}, {3, 1}) == std::vector<float>{0.7f});
}

TEST_CASE("A default camera has no reflection pass or texture", "[renderer][ssr]")
{
    RendererTestApp test;
    test.AddCamera().GetComponent<Fenrir::Camera>().bloom.intensity = 0.0f;
    test.AddMeshRenderer(test.Mesh(), test.Material());

    test.Frame();
    INFO("reflections are off by default");
    REQUIRE_FALSE(Fenrir::Camera{}.reflections.enabled);
    INFO("the prepass, the scene and Output only");
    REQUIRE(Labels(test.Passes()) ==
            std::vector<std::string>{"BrdfLut", "DepthPrepass", "Opaque", "Background", "Output"});
    INFO("and nothing kept for it");
    REQUIRE(RendererProbe::Reflections(test.renderer).empty());
}

TEST_CASE("A reflecting camera's HiZ passes follow its prepass and its SceneHistory passes end its scene",
          "[renderer][ssr]")
{
    RendererTestApp test;
    AddReflectingCamera(test);

    test.Frame();
    std::vector<std::string> expected{"BrdfLut", "DepthPrepass"};
    expected.insert(expected.end(), 11, "HiZ");
    expected.insert(expected.end(), {"ReflectionTrace", "Opaque", "Background"});
    expected.insert(expected.end(), 11, "SceneHistory");
    expected.emplace_back("Output");
    INFO("one HiZ and one SceneHistory pass per level of a 1280 x 720 chain, and the trace between");
    REQUIRE(Labels(test.Passes()) == expected);
}

TEST_CASE("The HiZ passes follow GTAO's, and the SceneHistory passes come before bloom's", "[renderer][ssr]")
{
    RendererTestApp test;
    Fenrir::Camera& camera = AddReflectingCamera(test);
    camera.ambientOcclusion.enabled = true;
    camera.bloom.intensity = 0.5f;

    test.Frame();
    const std::vector<std::string> labels = Labels(test.Passes());
    INFO("after the last GTAO pass");
    REQUIRE(LastIndexOf(labels, "GtaoDenoise") < IndexOf(labels, "HiZ"));
    INFO("and before Opaque");
    REQUIRE(LastIndexOf(labels, "HiZ") < IndexOf(labels, "Opaque"));
    INFO("SceneHistory after the scene's last pass");
    REQUIRE(IndexOf(labels, "Background") < IndexOf(labels, "SceneHistory"));
    INFO("and before the first BloomDownsample");
    REQUIRE(LastIndexOf(labels, "SceneHistory") < IndexOf(labels, "BloomDownsample"));
}

TEST_CASE("The HiZ passes copy SceneDepth into the pyramid's level 0, then draw each level from the one above",
          "[renderer][ssr]")
{
    RendererTestApp test;
    AddReflectingCamera(test);

    test.Frame();
    const std::vector<PassRecord> passes = test.Passes();
    const PassRecord prepass = Labelled(passes, "DepthPrepass").at(0);
    const std::vector<PassRecord> hiZ = Labelled(passes, "HiZ");
    REQUIRE(hiZ.size() == 11);
    const TextureHandle pyramid = hiZ[0].pass.colour.at(0).texture;
    const auto desc = RendererProbe::Desc(test.Device(), pyramid);
    INFO("the pyramid is R32F, the view's size, with every level, kept across frames");
    REQUIRE((desc.format == TextureFormat::R32Float && desc.width == 1280 && desc.height == 720 &&
             desc.mipLevels == 11 && desc.samples == 1));
    REQUIRE(pyramid == RendererProbe::Reflections(test.renderer).at(0).pyramid);
    INFO("level 0 samples SceneDepth, all of it");
    REQUIRE(Samples(hiZ[0], prepass.pass.depth->texture));
    REQUIRE_FALSE(Source(test, hiZ[0]).mipLevel.has_value());
    for (std::uint32_t level = 0; level < 11; ++level)
    {
        INFO("level " << level << " draws into that mip of the pyramid, at its size");
        REQUIRE(hiZ[level].pass.colour.size() == 1);
        REQUIRE(hiZ[level].pass.colour[0].texture == pyramid);
        REQUIRE(hiZ[level].pass.colour[0].mipLevel == level);
        const PixelSize size = MipSize({1280, 720}, level);
        REQUIRE(hiZ[level].viewport == PixelRect{0, 0, size.width, size.height});
        if (level > 0)
        {
            INFO("reading the level above, alone");
            const BindingGroupEntry source = Source(test, hiZ[level]);
            REQUIRE(source.texture == pyramid);
            REQUIRE(source.mipLevel == std::optional<std::uint32_t>(level - 1));
        }
    }
}

TEST_CASE("The SceneHistory passes copy SceneColor into the history's level 0, then box-filter each level",
          "[renderer][ssr]")
{
    RendererTestApp test;
    AddReflectingCamera(test);

    test.Frame();
    const std::vector<PassRecord> passes = test.Passes();
    const PassRecord opaque = Labelled(passes, "Opaque").at(0);
    const std::vector<PassRecord> history = Labelled(passes, "SceneHistory");
    REQUIRE(history.size() == 11);
    const TextureHandle texture = history[0].pass.colour.at(0).texture;
    const auto desc = RendererProbe::Desc(test.Device(), texture);
    INFO("the history is RGBA16F, the view's size, with every level, kept across frames");
    REQUIRE((desc.format == TextureFormat::Rgba16Float && desc.width == 1280 && desc.height == 720 &&
             desc.mipLevels == 11 && desc.samples == 1));
    REQUIRE(texture == RendererProbe::Reflections(test.renderer).at(0).history);
    INFO("level 0 samples SceneColor");
    REQUIRE(Samples(history[0], opaque.pass.colour.at(0).texture));
    for (std::uint32_t level = 1; level < 11; ++level)
    {
        INFO("level " << level << " draws into its mip from the level above alone");
        REQUIRE(history[level].pass.colour.at(0).texture == texture);
        REQUIRE(history[level].pass.colour.at(0).mipLevel == level);
        REQUIRE(Source(test, history[level]).mipLevel == std::optional<std::uint32_t>(level - 1));
    }
}

TEST_CASE("Under MSAA the reflection copies read the multisampled scene targets", "[renderer][ssr]")
{
    Fenrir::RendererConfig config = RendererTest::NullBackend();
    config.msaa = 4;
    RendererTestApp test(config);
    AddReflectingCamera(test);

    test.Frame();
    const std::vector<PassRecord> passes = test.Passes();
    const auto keys = [&](const PassRecord& pass) { return RendererProbe::Keys(test.Device(), pass.pipeline); };
    INFO("HiZ's copy reads sample 0 of SceneDepth through its MSAA variant");
    REQUIRE(keys(Labelled(passes, "HiZ").at(0)) == std::vector<std::string>{"COPY", "MSAA"});
    INFO("SceneHistory's copy averages SceneColor's samples through its MSAA variant");
    REQUIRE(keys(Labelled(passes, "SceneHistory").at(0)) == std::vector<std::string>{"MSAA"});
    INFO("the levels below read single-sampled levels");
    REQUIRE(keys(Labelled(passes, "HiZ").at(1)).empty());
    REQUIRE(keys(Labelled(passes, "SceneHistory").at(1)) == std::vector<std::string>{"DOWNSAMPLE"});
    INFO("and the kept textures are single-sampled");
    REQUIRE(RendererProbe::Desc(test.Device(), RendererProbe::Reflections(test.renderer).at(0).history).samples == 1);
}

TEST_CASE("Each reflecting camera keeps its own history, its pyramids swapping each frame", "[renderer][ssr]")
{
    RendererTestApp test;
    Fenrir::Camera& left = AddReflectingCamera(test);
    left.viewport = {0.0f, 0.0f, 0.5f, 1.0f};
    Fenrir::Camera& right = test.AddCamera().GetComponent<Fenrir::Camera>();
    right.viewport = {0.5f, 0.0f, 0.5f, 1.0f};
    right.order = 1;
    right.bloom.intensity = 0.0f;
    right.reflections.enabled = true;

    test.Frame();
    const auto first = RendererProbe::Reflections(test.renderer);
    INFO("two cameras, two sets of textures");
    REQUIRE(first.size() == 2);
    REQUIRE(first[0].history != first[1].history);
    REQUIRE(first[0].pyramid != first[1].pyramid);
    INFO("each view's passes draw into its own");
    const std::vector<PassRecord> passes = test.Passes();
    REQUIRE(Labelled(passes, "HiZ").size() == 2 * MipCount(640, 720));
    REQUIRE(Labelled(passes, "SceneHistory").size() == 2 * MipCount(640, 720));

    test.Frame();
    const auto second = RendererProbe::Reflections(test.renderer);
    INFO("the next frame keeps the history, and draws the other pyramid");
    REQUIRE(second.size() == 2);
    REQUIRE(second[0].history == first[0].history);
    REQUIRE(second[0].pyramid == first[0].previousPyramid);
    REQUIRE(second[0].previousPyramid == first[0].pyramid);
    REQUIRE(Labelled(test.Passes(), "HiZ").at(0).pass.colour.at(0).texture == second[0].pyramid);

    test.Frame();
    INFO("and the frame after swaps back");
    REQUIRE(RendererProbe::Reflections(test.renderer).at(0).pyramid == first[0].pyramid);
}

TEST_CASE("A camera's reflection textures are freed when it goes or turns them off", "[renderer][ssr]")
{
    RendererTestApp test;
    Fenrir::Entity reflecting = test.AddCamera();
    Fenrir::Camera& camera = reflecting.GetComponent<Fenrir::Camera>();
    camera.bloom.intensity = 0.0f;
    camera.reflections.enabled = true;
    test.AddMeshRenderer(test.Mesh(), test.Material());
    // Whether every texture `kept` names is live.
    const auto live = [&](const RendererProbe::ReflectionTextures& kept) {
        return test.Device().IsLive(kept.history) && test.Device().IsLive(kept.pyramid) &&
               test.Device().IsLive(kept.previousPyramid);
    };

    test.Frame();
    INFO("a reflecting camera keeps a history and two pyramids");
    const auto first = RendererProbe::Reflections(test.renderer);
    REQUIRE(first.size() == 1);
    REQUIRE(first[0].camera == reflecting.GetId());
    REQUIRE(live(first[0]));

    camera.reflections.enabled = false;
    test.Frame();
    INFO("off, it keeps none");
    REQUIRE(RendererProbe::Reflections(test.renderer).empty());
    REQUIRE_FALSE(test.Device().IsLive(first[0].history));
    REQUIRE_FALSE(test.Device().IsLive(first[0].pyramid));
    REQUIRE_FALSE(test.Device().IsLive(first[0].previousPyramid));

    camera.reflections.enabled = true;
    test.Frame();
    const auto again = RendererProbe::Reflections(test.renderer);
    REQUIRE(again.size() == 1);
    REQUIRE(live(again[0]));
    test.ActiveScene().GetEntityList().DestroyEntity(reflecting.GetId());
    test.Frame();
    INFO("gone, it keeps none");
    REQUIRE(RendererProbe::Reflections(test.renderer).empty());
    REQUIRE_FALSE(test.Device().IsLive(again[0].history));
    REQUIRE_FALSE(test.Device().IsLive(again[0].pyramid));
    REQUIRE_FALSE(test.Device().IsLive(again[0].previousPyramid));
}

TEST_CASE("A reflecting view's textures are remade at its new size when the window resizes", "[renderer][ssr]")
{
    RendererTestApp test;
    AddReflectingCamera(test);
    test.Frame();
    const TextureHandle before = RendererProbe::Reflections(test.renderer).at(0).history;

    RendererProbe::SetSurfaceSize(test.renderer, {640, 480});
    test.Frame();
    const TextureHandle after = RendererProbe::Reflections(test.renderer).at(0).history;
    INFO("a new history at the new size, with its levels");
    REQUIRE(after != before);
    const auto desc = RendererProbe::Desc(test.Device(), after);
    REQUIRE((desc.width == 640 && desc.height == 480 && desc.mipLevels == MipCount(640, 480)));
}

TEST_CASE("A camera's history is invalid on its first frame and for one frame after a resize", "[renderer][ssr]")
{
    HistoryCore core;
    const Fenrir::Math::Mat4 first(2.0f);
    const Fenrir::Math::Mat4 second(3.0f);
    const Fenrir::Math::Mat4 third(4.0f);

    const Fenrir::Render::HistoryView made = core.Frame(ReflectingView(7, {64, 32}), first);
    INFO("the first frame has nothing of a frame before");
    REQUIRE_FALSE(made.IsEmpty());
    REQUIRE_FALSE(made.valid);
    REQUIRE(made.mips == 7);

    const Fenrir::Render::HistoryView kept = core.Frame(ReflectingView(7, {64, 32}), second);
    INFO("the next holds the first's, seen by the first's view-projection");
    REQUIRE(kept.valid);
    REQUIRE(kept.previousViewProjection == first);

    const Fenrir::Render::HistoryView resized = core.Frame(ReflectingView(7, {48, 32}), third);
    INFO("resized, the textures are remade and hold nothing");
    REQUIRE_FALSE(resized.valid);
    REQUIRE(resized.mips == 6);

    INFO("and the frame after is valid again");
    REQUIRE(core.Frame(ReflectingView(7, {48, 32}), first).valid);
}

TEST_CASE("A camera whose view comes twice in a frame keeps one history, for its first view", "[renderer][ssr]")
{
    HistoryCore core;
    const Fenrir::Render::ViewRequest view = ReflectingView(3, {16, 16});
    const std::vector<Fenrir::Render::ViewRequest> views{view, view};
    Fenrir::Render::RenderGraph graph;
    core.history.BeginFrame(views);
    INFO("the first gets the textures");
    REQUIRE_FALSE(core.history.Import(graph, views[0], {16, 16}, Fenrir::Math::Mat4(1.0f)).IsEmpty());
    INFO("the second none, rather than drawing over them");
    REQUIRE(core.history.Import(graph, views[1], {16, 16}, Fenrir::Math::Mat4(1.0f)).IsEmpty());
    core.history.EndFrame();
}
