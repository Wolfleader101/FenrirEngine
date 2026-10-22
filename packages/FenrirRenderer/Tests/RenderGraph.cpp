#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Core/RenderGraph.hpp"
#include "Core/TransientPool.hpp"
#include "Device/GraphicsDevice.hpp"
#include "RendererProbe.hpp"

using Fenrir::RendererProbe;
using Fenrir::Graphics::LoadOp;
using Fenrir::Graphics::QueueKind;
using Fenrir::Graphics::TextureDesc;
using Fenrir::Graphics::TextureFormat;
using Fenrir::Graphics::TextureHandle;
using Fenrir::Render::GraphCompilation;
using Fenrir::Render::GraphResource;
using Fenrir::Render::PassBuilder;
using Fenrir::Render::PassContext;
using Fenrir::Render::RenderGraph;
using Fenrir::Render::RenderStage;
using Fenrir::Render::TransientPool;

namespace
{
    TextureDesc Target(TextureFormat format = TextureFormat::Rgba16Float)
    {
        TextureDesc desc;
        desc.format = format;
        desc.width = 64;
        desc.height = 32;
        return desc;
    }

    std::vector<std::string> Names(const RenderGraph& graph, const std::vector<std::uint32_t>& passes)
    {
        std::vector<std::string> names;
        for (std::uint32_t pass : passes)
        {
            names.push_back(graph.PassName(pass));
        }
        return names;
    }

    /** @brief A pass that loads `target` and draws into it, so it keeps what earlier passes drew. */
    void DrawOnto(RenderGraph& graph, std::string name, RenderStage stage, GraphResource target)
    {
        graph.AddPass(
            std::move(name), stage, QueueKind::Graphics,
            [target](PassBuilder& pass) { pass.ColourAttachment(target, LoadOp::Load); }, nullptr);
    }
} // namespace

TEST_CASE("Render passes run by render stage, then in registration order", "[renderer][graph]")
{
    RenderGraph graph;
    const GraphResource backbuffer = graph.ImportTexture("Backbuffer", TextureHandle{1, 1}, Target());
    DrawOnto(graph, "Overlay", RenderStage::Overlay, backbuffer);
    DrawOnto(graph, "Opaque A", RenderStage::Opaque, backbuffer);
    DrawOnto(graph, "Debug", RenderStage::Debug, backbuffer);
    DrawOnto(graph, "Opaque B", RenderStage::Opaque, backbuffer);
    DrawOnto(graph, "Shadow", RenderStage::Shadow, backbuffer);

    const GraphCompilation compiled = graph.Compile();

    INFO("stages in their fixed order; the two opaque passes in the order they were added");
    REQUIRE(compiled.Ok());
    REQUIRE(Names(graph, compiled.order) ==
            std::vector<std::string>{"Shadow", "Opaque A", "Opaque B", "Debug", "Overlay"});
}

TEST_CASE("A pass reading what a pass in a later stage writes runs after it", "[renderer][graph]")
{
    RenderGraph graph;
    const GraphResource backbuffer = graph.ImportTexture("Backbuffer", TextureHandle{1, 1}, Target());
    const GraphResource imageTarget = graph.ImportTexture("Mirror", TextureHandle{2, 1}, Target());
    // The window's view samples the mirror camera's image in Opaque; the mirror view writes it in its Output.
    graph.AddPass(
        "Window Opaque", RenderStage::Opaque, QueueKind::Graphics,
        [&](PassBuilder& pass) {
            pass.Sample(imageTarget);
            pass.ColourAttachment(backbuffer);
        },
        nullptr);
    DrawOnto(graph, "Window Debug", RenderStage::Debug, backbuffer);
    graph.AddPass(
        "Mirror Output", RenderStage::Output, QueueKind::Graphics,
        [&](PassBuilder& pass) { pass.ColourAttachment(imageTarget); }, nullptr);

    const GraphCompilation compiled = graph.Compile();

    INFO("the mirror's output runs first, the window's opaque pass after it, and the rest keep their order");
    REQUIRE(compiled.Ok());
    REQUIRE(Names(graph, compiled.order) == std::vector<std::string>{"Mirror Output", "Window Opaque", "Window Debug"});
}

TEST_CASE("A pass whose writes nothing reads is culled", "[renderer][graph]")
{
    RenderGraph graph;
    const GraphResource backbuffer = graph.ImportTexture("Backbuffer", TextureHandle{1, 1}, Target());
    const GraphResource sceneColour = graph.CreateTexture("SceneColor", Target());
    const GraphResource unread = graph.CreateTexture("Unread", Target());
    graph.AddPass(
        "Opaque", RenderStage::Opaque, QueueKind::Graphics,
        [&](PassBuilder& pass) { pass.ColourAttachment(sceneColour); }, nullptr);
    graph.AddPass(
        "Unused", RenderStage::PostProcess, QueueKind::Graphics,
        [&](PassBuilder& pass) {
            pass.Sample(sceneColour);
            pass.ColourAttachment(unread);
        },
        nullptr);
    graph.AddPass(
        "Output", RenderStage::Output, QueueKind::Graphics,
        [&](PassBuilder& pass) {
            pass.Sample(sceneColour);
            pass.ColourAttachment(backbuffer);
        },
        nullptr);

    const GraphCompilation compiled = graph.Compile();

    INFO("the pass writing a texture nothing reads is culled; the passes feeding the backbuffer run");
    REQUIRE(compiled.Ok());
    REQUIRE(Names(graph, compiled.culled) == std::vector<std::string>{"Unused"});
    REQUIRE(Names(graph, compiled.order) == std::vector<std::string>{"Opaque", "Output"});
}

TEST_CASE("A transient's earlier write that a later clear hides is culled", "[renderer][graph]")
{
    RenderGraph graph;
    const GraphResource backbuffer = graph.ImportTexture("Backbuffer", TextureHandle{1, 1}, Target());
    const GraphResource sceneColour = graph.CreateTexture("SceneColor", Target());
    graph.AddPass(
        "Hidden", RenderStage::Opaque, QueueKind::Graphics,
        [&](PassBuilder& pass) { pass.ColourAttachment(sceneColour); }, nullptr);
    graph.AddPass(
        "Clears", RenderStage::Background, QueueKind::Graphics,
        [&](PassBuilder& pass) { pass.ColourAttachment(sceneColour, LoadOp::Clear); }, nullptr);
    graph.AddPass(
        "Output", RenderStage::Output, QueueKind::Graphics,
        [&](PassBuilder& pass) {
            pass.Sample(sceneColour);
            pass.ColourAttachment(backbuffer);
        },
        nullptr);

    const GraphCompilation compiled = graph.Compile();

    INFO("what the first pass drew is cleared before anything reads it, so it is culled");
    REQUIRE(Names(graph, compiled.culled) == std::vector<std::string>{"Hidden"});
}

TEST_CASE("A pass reading a resource nothing wrote is an error", "[renderer][graph]")
{
    RenderGraph graph;
    const GraphResource backbuffer = graph.ImportTexture("Backbuffer", TextureHandle{1, 1}, Target());
    const GraphResource neverWritten = graph.CreateTexture("ShadowMap", Target(TextureFormat::Depth32Float));
    graph.AddPass(
        "Opaque", RenderStage::Opaque, QueueKind::Graphics,
        [&](PassBuilder& pass) {
            pass.Sample(neverWritten);
            pass.ColourAttachment(backbuffer);
        },
        nullptr);

    const GraphCompilation compiled = graph.Compile();

    INFO("compiling fails with one error naming the pass and the resource");
    REQUIRE_FALSE(compiled.Ok());
    REQUIRE(compiled.errors.size() == 1);
    REQUIRE(compiled.errors[0].find("Opaque") != std::string::npos);
    REQUIRE(compiled.errors[0].find("ShadowMap") != std::string::npos);

    auto device = Fenrir::Graphics::CreateNullDevice();
    TransientPool pool(device->FramesInFlight());
    pool.BeginFrame(0);
    INFO("a graph with errors records nothing");
    REQUIRE(graph.Record(compiled, *device, pool, 0).empty());
}

TEST_CASE("Two transients with equal descriptions and disjoint lifetimes share a pool entry", "[renderer][graph]")
{
    auto device = Fenrir::Graphics::CreateNullDevice();
    device->BeginFrame(0);
    TransientPool pool(device->FramesInFlight());
    pool.BeginFrame(0);

    RenderGraph graph;
    const GraphResource backbuffer = graph.ImportTexture("Backbuffer", device->Backbuffer(), Target());
    const GraphResource first = graph.CreateTexture("First", Target());
    const GraphResource second = graph.CreateTexture("Second", Target());
    TextureHandle firstTexture;
    TextureHandle secondTexture;
    graph.AddPass(
        "Write first", RenderStage::Opaque, QueueKind::Graphics,
        [&](PassBuilder& pass) { pass.ColourAttachment(first); },
        [&](PassContext& context) { firstTexture = context.Texture(first); });
    graph.AddPass(
        "Read first", RenderStage::Opaque, QueueKind::Graphics,
        [&](PassBuilder& pass) {
            pass.Sample(first);
            pass.ColourAttachment(backbuffer, LoadOp::Load);
        },
        nullptr);
    graph.AddPass(
        "Write second", RenderStage::Opaque, QueueKind::Graphics,
        [&](PassBuilder& pass) { pass.ColourAttachment(second); },
        [&](PassContext& context) { secondTexture = context.Texture(second); });
    graph.AddPass(
        "Read second", RenderStage::Opaque, QueueKind::Graphics,
        [&](PassBuilder& pass) {
            pass.Sample(second);
            pass.ColourAttachment(backbuffer, LoadOp::Load);
        },
        nullptr);

    const GraphCompilation compiled = graph.Compile();
    REQUIRE(compiled.Ok());
    const auto recorded = graph.Record(compiled, *device, pool, 0);

    INFO("every pass was recorded");
    REQUIRE(recorded.size() == 4);
    INFO("the second transient, first used after the first's last use, got the first's texture");
    REQUIRE_FALSE(firstTexture.IsEmpty());
    REQUIRE(secondTexture == firstTexture);
    INFO("the pool holds one entry");
    REQUIRE(RendererProbe::TransientCount(pool) == 1);
}

TEST_CASE("Two transients in use at once take separate pool entries", "[renderer][graph]")
{
    auto device = Fenrir::Graphics::CreateNullDevice();
    device->BeginFrame(0);
    TransientPool pool(device->FramesInFlight());
    pool.BeginFrame(0);

    RenderGraph graph;
    const GraphResource backbuffer = graph.ImportTexture("Backbuffer", device->Backbuffer(), Target());
    const GraphResource first = graph.CreateTexture("First", Target());
    const GraphResource second = graph.CreateTexture("Second", Target());
    TextureHandle firstTexture;
    TextureHandle secondTexture;
    graph.AddPass(
        "Write both", RenderStage::Opaque, QueueKind::Graphics,
        [&](PassBuilder& pass) {
            pass.ColourAttachment(first);
            pass.ColourAttachment(second);
        },
        [&](PassContext& context) {
            firstTexture = context.Texture(first);
            secondTexture = context.Texture(second);
        });
    graph.AddPass(
        "Read both", RenderStage::Output, QueueKind::Graphics,
        [&](PassBuilder& pass) {
            pass.Sample(first);
            pass.Sample(second);
            pass.ColourAttachment(backbuffer);
        },
        nullptr);

    const auto recorded = graph.Record(graph.Compile(), *device, pool, 0);

    INFO("overlapping lifetimes need two textures");
    REQUIRE(recorded.size() == 2);
    REQUIRE(firstTexture != secondTexture);
    REQUIRE(RendererProbe::TransientCount(pool) == 2);
}

TEST_CASE("An imported resource is never pooled", "[renderer][graph]")
{
    auto device = Fenrir::Graphics::CreateNullDevice();
    device->BeginFrame(0);
    TransientPool pool(device->FramesInFlight());
    pool.BeginFrame(0);
    const TextureHandle imageTarget = device->CreateTexture(Target());

    RenderGraph graph;
    const GraphResource image = graph.ImportTexture("Image", imageTarget, Target());
    TextureHandle seen;
    graph.AddPass(
        "Output", RenderStage::Output, QueueKind::Graphics, [&](PassBuilder& pass) { pass.ColourAttachment(image); },
        [&](PassContext& context) { seen = context.Texture(image); });

    const auto recorded = graph.Record(graph.Compile(), *device, pool, 0);
    pool.EndFrame(*device);

    INFO("the pass drew into the imported texture itself");
    REQUIRE(recorded.size() == 1);
    REQUIRE(seen == imageTarget);
    INFO("the pool made and holds nothing for it");
    REQUIRE(RendererProbe::TransientCount(pool) == 0);
    INFO("and ending the frame left it live");
    REQUIRE(device->IsLive(imageTarget));
}

TEST_CASE("A recorded pass carries its attachments and declared uses down to the device", "[renderer][graph]")
{
    auto device = Fenrir::Graphics::CreateNullDevice();
    device->BeginFrame(0);
    TransientPool pool(device->FramesInFlight());
    pool.BeginFrame(0);

    RenderGraph graph;
    const GraphResource backbuffer = graph.ImportTexture("Backbuffer", device->Backbuffer(), Target());
    const GraphResource depth = graph.CreateTexture("SceneDepth", Target(TextureFormat::Depth32Float));
    graph.AddPass(
        "Opaque", RenderStage::Opaque, QueueKind::Graphics,
        [&](PassBuilder& pass) {
            pass.ColourAttachment(backbuffer, LoadOp::Clear, {0.25f, 0.5f, 0.75f, 1.0f});
            pass.DepthAttachment(depth, LoadOp::Clear, 0.0f);
        },
        nullptr);

    const auto recorded = graph.Record(graph.Compile(), *device, pool, 0);
    REQUIRE(recorded.size() == 1);
    const auto& begin = std::get<Fenrir::Graphics::CmdBeginPass>(recorded[0].commands.commands.front()).pass;

    INFO("the pass is named and on the graphics queue");
    REQUIRE(begin.label == "Opaque");
    REQUIRE(begin.queue == QueueKind::Graphics);
    INFO("the backbuffer is its colour attachment, cleared to the given colour");
    REQUIRE(begin.colour.size() == 1);
    REQUIRE(begin.colour[0].texture == device->Backbuffer());
    REQUIRE(begin.colour[0].clear == std::array<float, 4>{0.25f, 0.5f, 0.75f, 1.0f});
    INFO("the transient depth is its depth attachment, written and cleared to the far plane");
    REQUIRE(begin.depth.has_value());
    REQUIRE(device->IsLive(begin.depth->texture));
    REQUIRE_FALSE(begin.depth->readOnly);
    REQUIRE(begin.depth->clear == 0.0f);
    INFO("both uses are declared for the backend's barriers");
    REQUIRE(begin.uses.size() == 2);
    REQUIRE(begin.uses[0].usage == Fenrir::Graphics::ResourceUsage::ColourAttachment);
    REQUIRE(begin.uses[1].usage == Fenrir::Graphics::ResourceUsage::DepthWrite);
}
