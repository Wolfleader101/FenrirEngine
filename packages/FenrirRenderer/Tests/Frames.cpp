#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Core/FrameRingBuffer.hpp"
#include "Core/RendererCore.hpp"
#include "FakeSurface.hpp"
#include "RendererProbe.hpp"

using Fenrir::RendererProbe;
using namespace Fenrir::Graphics;
using namespace Fenrir::Render;

namespace
{
    /** @brief A render feature whose passes the test gives as a function. */
    class TestFeature final : public RenderFeature
    {
      public:
        explicit TestFeature(std::function<void(RenderGraph&, const FrameContext&)> addPasses)
            : m_addPasses(std::move(addPasses))
        {
        }

        void AddPasses(RenderGraph& graph, const FrameContext& frame) override
        {
            m_addPasses(graph, frame);
        }

      private:
        std::function<void(RenderGraph&, const FrameContext&)> m_addPasses;
    };

    std::unique_ptr<RenderFeature> Feature(std::function<void(RenderGraph&, const FrameContext&)> addPasses)
    {
        return std::make_unique<TestFeature>(std::move(addPasses));
    }

    /** @brief The labels of the passes in what the device received, in order. */
    std::vector<std::string> SubmittedPasses(const RendererCore& core)
    {
        std::vector<std::string> labels;
        for (const CommandList& list : RendererProbe::Submitted(core))
        {
            for (const Command& command : list.commands)
            {
                if (const auto* begin = std::get_if<CmdBeginPass>(&command))
                {
                    labels.push_back(begin->pass.label);
                }
            }
        }
        return labels;
    }

    /** @brief A scene's worth of passes: opaque and background into SceneColor, output to the backbuffer. */
    void AddViewPasses(RenderGraph& graph, const FrameContext& frame)
    {
        TextureDesc colour;
        colour.format = TextureFormat::Rgba16Float;
        colour.width = frame.size.width;
        colour.height = frame.size.height;
        const GraphResource sceneColour = graph.CreateTexture("SceneColor", colour);
        graph.AddPass(
            "Background", RenderStage::Background, QueueKind::Graphics,
            [=](PassBuilder& pass) { pass.ColourAttachment(sceneColour, LoadOp::Load); },
            [](PassContext& context) { context.Encoder().Draw({3}); });
        graph.AddPass(
            "Opaque", RenderStage::Opaque, QueueKind::Graphics,
            [=](PassBuilder& pass) { pass.ColourAttachment(sceneColour, LoadOp::Clear); },
            [](PassContext& context) {
                context.Encoder().Draw({36});
                context.Encoder().Draw({36});
            });
        graph.AddPass(
            "Output", RenderStage::Output, QueueKind::Graphics,
            [=](PassBuilder& pass) {
                pass.Sample(sceneColour);
                pass.ColourAttachment(frame.backbuffer, LoadOp::DontCare);
            },
            [](PassContext& context) { context.Encoder().Draw({3}); });
    }
} // namespace

TEST_CASE("A frame submits each render feature's passes in order, one list per pass, then presents", "[renderer][core]")
{
    RendererTest::FakeSurface surface;
    RendererCore core(CreateNullDevice(), surface);
    core.AddFeature(Feature(AddViewPasses));

    const FrameReport report = core.RenderFrame();

    INFO("the frame read the surface's size and was drawn");
    REQUIRE(surface.sizeReads == 1);
    REQUIRE(report.drawn);
    REQUIRE(report.errors.empty());
    INFO("the passes ran by stage, each in its own CommandList");
    REQUIRE(SubmittedPasses(core) == std::vector<std::string>{"Opaque", "Background", "Output"});
    REQUIRE(RendererProbe::Submitted(core).size() == 3);
    REQUIRE(report.passesRun == 3);
    INFO("and the frame was presented once");
    REQUIRE(RendererProbe::Presents(core.Device()) == 1);
}

TEST_CASE("The backbuffer a feature imports is the device's, at the surface's pixel size", "[renderer][core]")
{
    RendererTest::FakeSurface surface;
    surface.size = {320, 200};
    RendererCore core(CreateNullDevice(), surface);
    PixelSize seenSize;
    TextureHandle drawnInto;
    core.AddFeature(Feature([&](RenderGraph& graph, const FrameContext& frame) {
        seenSize = frame.size;
        graph.AddPass(
            "Overlay", RenderStage::Overlay, QueueKind::Graphics,
            [&](PassBuilder& pass) { pass.ColourAttachment(frame.backbuffer); },
            [&](PassContext& context) { drawnInto = context.Texture(frame.backbuffer); });
    }));

    core.RenderFrame();

    INFO("the feature saw the surface's size and drew into the device's backbuffer");
    REQUIRE(seenSize == PixelSize{320, 200});
    REQUIRE(drawnInto == core.Device().Backbuffer());
}

TEST_CASE("An empty surface submits and presents nothing, but the frame index advances", "[renderer][core]")
{
    RendererTest::FakeSurface surface;
    RendererCore core(CreateNullDevice({.framesInFlight = 2}), surface);
    bool featureAsked = false;
    core.AddFeature(Feature([&](RenderGraph& graph, const FrameContext& frame) {
        featureAsked = true;
        AddViewPasses(graph, frame);
    }));
    const BufferHandle destroyed = core.Device().CreateBuffer({64, BufferUsage::Uniform});
    REQUIRE(core.Device().Destroy(destroyed));

    surface.size = {0, 0};
    const FrameReport first = core.RenderFrame();
    const FrameReport second = core.RenderFrame();

    INFO("neither minimised frame built, submitted or presented anything");
    REQUIRE_FALSE(first.drawn);
    REQUIRE_FALSE(second.drawn);
    REQUIRE_FALSE(featureAsked);
    REQUIRE(RendererProbe::Submitted(core).empty());
    REQUIRE(RendererProbe::Presents(core.Device()) == 0);
    INFO("but the frame index still advanced");
    REQUIRE(first.frameIndex == 0);
    REQUIRE(second.frameIndex == 1);
    REQUIRE(core.FrameIndex() == 2);

    core.RenderFrame();
    INFO("so deferred destruction kept going: the buffer's slot is freed in frame 2");
    REQUIRE(RendererProbe::PendingBuffers(core.Device()) == 0);
}

TEST_CASE("A frame whose render graph has errors submits nothing and reports them", "[renderer][core]")
{
    RendererTest::FakeSurface surface;
    RendererCore core(CreateNullDevice(), surface);
    core.AddFeature(Feature([](RenderGraph& graph, const FrameContext& frame) {
        const GraphResource never = graph.CreateTexture("Never", TextureDesc{});
        graph.AddPass(
            "Reads never", RenderStage::Output, QueueKind::Graphics,
            [&](PassBuilder& pass) {
                pass.Sample(never);
                pass.ColourAttachment(frame.backbuffer);
            },
            nullptr);
    }));

    const FrameReport report = core.RenderFrame();

    INFO("the error is reported and nothing reached the device");
    REQUIRE_FALSE(report.drawn);
    REQUIRE(report.errors.size() == 1);
    REQUIRE(RendererProbe::Submitted(core).empty());
    REQUIRE(RendererProbe::Presents(core.Device()) == 0);
}

TEST_CASE("A frame's transients are kept between frames, one set per frame in flight", "[renderer][core]")
{
    RendererTest::FakeSurface surface;
    RendererCore core(CreateNullDevice({.framesInFlight = 2}), surface);
    core.AddFeature(Feature(AddViewPasses));

    for (int frame = 0; frame < 4; ++frame)
    {
        core.RenderFrame();
    }

    INFO("SceneColor was made once for each of the two frames in flight, and reused after");
    REQUIRE(RendererProbe::TransientCount(core) == 2);
    INFO("those two and the backbuffer are the only live textures");
    REQUIRE(RendererProbe::LiveTextures(core.Device()) == 3);
}

TEST_CASE("A feature's per-frame ring buffer is bound by the frame being recorded", "[renderer][core]")
{
    RendererTest::FakeSurface surface;
    RendererCore core(CreateNullDevice({.framesInFlight = 2}), surface);
    FrameRingBuffer ring(core.Device(), {256, BufferUsage::Vertex});
    core.AddFeature(Feature([&ring](RenderGraph& graph, const FrameContext& frame) {
        graph.AddPass(
            "Debug", RenderStage::Debug, QueueKind::Graphics,
            [&](PassBuilder& pass) { pass.ColourAttachment(frame.backbuffer, LoadOp::Load); },
            [&ring](PassContext& context) {
                context.Encoder().SetVertexBuffer(0, ring.Current(context.FrameIndex()));
                context.Encoder().Draw({2});
            });
    }));

    std::vector<BufferHandle> bound;
    for (int frame = 0; frame < 3; ++frame)
    {
        core.RenderFrame();
        for (const Command& command : RendererProbe::Submitted(core).at(0).commands)
        {
            if (const auto* set = std::get_if<CmdSetVertexBuffer>(&command))
            {
                bound.push_back(set->buffer);
            }
        }
    }

    INFO("frames 0, 1 and 2 bound the first copy, the second, then the first again");
    REQUIRE(bound.size() == 3);
    REQUIRE(bound[0] != bound[1]);
    REQUIRE(bound[2] == bound[0]);
}
