// Draws through pipelines made from embedded shader containers, read back from offscreen targets.

#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Device/CommandList.hpp"
#include "FenrirShaders/GlFullscreenFill.hpp"
#include "FenrirShaders/GlPositionFill.hpp"
#include "GlProbe.hpp"
#include "GlTestApp.hpp"

using Fenrir::GlProbe;
using namespace Fenrir::Graphics;

namespace
{
    constexpr std::uint32_t Size = GlTest::TargetSize;
    constexpr GlProbe::Pixel Black{0.0f, 0.0f, 0.0f, 1.0f};
    constexpr GlProbe::Pixel Fill{0.2f, 0.6f, 0.4f, 1.0f};

    /** @brief A fill shader's pipeline, its colour block and the binding group holding it at set 1. */
    struct FillDraw
    {
        PipelineHandle pipeline;
        BindingGroupHandle fill;
    };

    /**
     * @brief Makes a pipeline from `container`'s base variant drawing into one RGBA8 target with no depth, and a
     * binding group of the Fill block at set 1 holding `colour`.
     */
    FillDraw MakeFillDraw(GraphicsDevice& device, const Fenrir::ShaderContainer& container, CullMode cull,
                          std::vector<VertexBufferLayout> vertexBuffers = {})
    {
        const BindingGroupLayoutHandle layout =
            device.CreateBindingGroupLayout({{{0, Fenrir::ShaderBindingKind::UniformBuffer}}});
        PipelineDesc desc;
        desc.shader = &container.variants.at(0);
        desc.sets.at(1) = layout;
        desc.vertexBuffers = std::move(vertexBuffers);
        desc.cull = cull;
        desc.depth.test = false;
        desc.depth.write = false;
        desc.colourFormats = {TextureFormat::Rgba8Unorm};
        desc.depthFormat.reset();
        const PipelineResult made = device.CreatePipeline(desc, container.name);
        INFO("the pipeline is made: " << made.diagnostic);
        REQUIRE_FALSE(made.pipeline.IsEmpty());

        const BufferHandle block =
            device.CreateBuffer({sizeof(Fill), BufferUsage::Uniform}, GlTest::BytesOf(Fill), "Fill block");
        BindingGroupEntry entry;
        entry.binding = 0;
        entry.buffer = block;
        return {made.pipeline, device.CreateBindingGroup({layout, {entry}})};
    }

    /** @brief A pass clearing `target` to black. */
    PassDesc ClearToBlack(TextureHandle target)
    {
        PassDesc pass;
        pass.label = "Draw";
        pass.colour.push_back({target, LoadOp::Clear, Black});
        pass.uses.push_back({target, {}, ResourceUsage::ColourAttachment});
        return pass;
    }
} // namespace

TEST_CASE("A pass drawing a full-screen triangle from an embedded shader container reads back its colour",
          "[renderer][gl]")
{
    GlTest::GlTestApp test;
    test.RequireOpenGL();
    GraphicsDevice& device = *test.device;
    device.BeginFrame(0);
    const TextureHandle target = test.MakeTarget();
    const Fenrir::ShaderContainer container = GlTest::ReadContainer(Fenrir::Shaders::GlFullscreenFill);
    const FillDraw draw = MakeFillDraw(device, container, CullMode::None);

    CommandList list;
    {
        PassEncoder encoder(list, ClearToBlack(target));
        encoder.SetPipeline(draw.pipeline);
        encoder.SetBindingGroup(1, draw.fill);
        encoder.Draw({.vertexCount = 3});
    }
    device.Submit(list);
    const GlProbe::Image image = GlProbe::Read(device, target);

    INFO("every corner and the centre hold the colour the shader read from its uniform block at set 1");
    REQUIRE(GlProbe::Near(image.At(0, 0), Fill));
    REQUIRE(GlProbe::Near(image.At(Size - 1, 0), Fill));
    REQUIRE(GlProbe::Near(image.At(0, Size - 1), Fill));
    REQUIRE(GlProbe::Near(image.At(Size - 1, Size - 1), Fill));
    REQUIRE(GlProbe::Near(image.At(Size / 2, Size / 2), Fill));
}

TEST_CASE("The top row of an offscreen target is row 0 of the read-back", "[renderer][gl]")
{
    GlTest::GlTestApp test;
    test.RequireOpenGL();
    GraphicsDevice& device = *test.device;
    device.BeginFrame(0);
    const TextureHandle target = test.MakeTarget();
    const Fenrir::ShaderContainer container = GlTest::ReadContainer(Fenrir::Shaders::GlPositionFill);
    const FillDraw draw = MakeFillDraw(device, container, CullMode::None, {{8, {{0, VertexFormat::Float2, 0}}}});
    // Two triangles over the upper half of clip space: y from 0 to 1, where +y is up (ADR-0016).
    const std::array<float, 12> upperHalf{-1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, -1.0f, 0.0f, 1.0f, 1.0f, -1.0f, 1.0f};
    const BufferHandle vertices = device.CreateBuffer({sizeof(upperHalf), BufferUsage::Vertex},
                                                      std::as_bytes(std::span(upperHalf)), "Upper half");

    CommandList list;
    {
        PassEncoder encoder(list, ClearToBlack(target));
        encoder.SetPipeline(draw.pipeline);
        encoder.SetBindingGroup(1, draw.fill);
        encoder.SetVertexBuffer(0, vertices);
        encoder.Draw({.vertexCount = 6});
    }
    device.Submit(list);
    const GlProbe::Image image = GlProbe::Read(device, target);

    INFO("row 0 of the read-back is the top row, which the upper half of clip space covers");
    REQUIRE(GlProbe::Near(image.At(0, 0), Fill));
    REQUIRE(GlProbe::Near(image.At(Size - 1, 0), Fill));
    REQUIRE(GlProbe::Near(image.At(Size / 2, Size / 2 - 1), Fill));
    INFO("the last row is the bottom row, which nothing drew");
    REQUIRE(GlProbe::Near(image.At(0, Size - 1), Black));
    REQUIRE(GlProbe::Near(image.At(Size / 2, Size / 2), Black));
}

TEST_CASE("A counter-clockwise triangle faces the front in an offscreen target", "[renderer][gl]")
{
    GlTest::GlTestApp test;
    test.RequireOpenGL();
    GraphicsDevice& device = *test.device;
    device.BeginFrame(0);
    const TextureHandle target = test.MakeTarget();
    const Fenrir::ShaderContainer container = GlTest::ReadContainer(Fenrir::Shaders::GlFullscreenFill);
    // The fixture's triangle is counter-clockwise as the viewer sees it; back faces are culled.
    const FillDraw draw = MakeFillDraw(device, container, CullMode::Back);

    CommandList list;
    {
        PassEncoder encoder(list, ClearToBlack(target));
        encoder.SetPipeline(draw.pipeline);
        encoder.SetBindingGroup(1, draw.fill);
        encoder.Draw({.vertexCount = 3});
    }
    device.Submit(list);
    const GlProbe::Image image = GlProbe::Read(device, target);

    INFO("the triangle is drawn, not culled as a back face");
    REQUIRE(GlProbe::Near(image.At(Size / 2, Size / 2), Fill));
}

TEST_CASE("A viewport counts pixels from the offscreen target's top-left corner", "[renderer][gl]")
{
    GlTest::GlTestApp test;
    test.RequireOpenGL();
    GraphicsDevice& device = *test.device;
    device.BeginFrame(0);
    const TextureHandle target = test.MakeTarget();
    const Fenrir::ShaderContainer container = GlTest::ReadContainer(Fenrir::Shaders::GlFullscreenFill);
    const FillDraw draw = MakeFillDraw(device, container, CullMode::None);

    CommandList list;
    {
        PassEncoder encoder(list, ClearToBlack(target));
        encoder.SetPipeline(draw.pipeline);
        encoder.SetBindingGroup(1, draw.fill);
        encoder.SetViewport({0, 0, Size / 2, Size / 2});
        encoder.Draw({.vertexCount = 3});
    }
    device.Submit(list);
    const GlProbe::Image image = GlProbe::Read(device, target);

    INFO("the full-screen triangle fills the top-left quarter, where the viewport is");
    REQUIRE(GlProbe::Near(image.At(0, 0), Fill));
    REQUIRE(GlProbe::Near(image.At(Size / 2 - 1, Size / 2 - 1), Fill));
    INFO("and nothing outside it");
    REQUIRE(GlProbe::Near(image.At(Size / 2, Size / 2), Black));
    REQUIRE(GlProbe::Near(image.At(Size - 1, 0), Black));
    REQUIRE(GlProbe::Near(image.At(0, Size - 1), Black));
}
