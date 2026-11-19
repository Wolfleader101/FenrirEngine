// Indirect draws on the OpenGL backend (ADR-0057), read back from offscreen targets: a multi-draw from a CPU-written
// command buffer lands each command's quad where its base instance's draw record puts it, through Fenrir/Draw.glsl's
// FenrirDrawIndex() unchanged; a command with no instances draws nothing; and a compute pass that writes the instance
// counts is made visible to the draw that follows by the barrier the backend derives from the passes' declared uses.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Device/CommandList.hpp"
#include "FenrirShaders/GlIndirectCounts.hpp"
#include "FenrirShaders/GlIndirectFill.hpp"
#include "GlProbe.hpp"
#include "GlTestApp.hpp"

using Fenrir::GlProbe;
using namespace Fenrir::Graphics;

namespace
{
    constexpr std::uint32_t Size = GlTest::TargetSize;
    constexpr GlProbe::Pixel Black{0.0f, 0.0f, 0.0f, 1.0f};
    constexpr GlProbe::Pixel Fill{0.2f, 0.6f, 0.4f, 1.0f};
    constexpr std::uint32_t CommandSize = sizeof(DrawIndexedIndirectCommand);

    /** @brief Fenrir/Draw.glsl's DrawItem as std430 lays it: 192 bytes, the world matrix first. */
    struct DrawRecord
    {
        std::array<float, 16> world{};
        std::array<float, 16> normal{};
        std::uint32_t paletteOffset = 0;
        float worldSign = 1.0f;
        std::uint32_t instanceFirst = 0;
        std::uint32_t instanceCount = 0;
        std::array<float, 4> colour{1.0f, 1.0f, 1.0f, 1.0f};
        std::array<float, 4> boundsMin{};
        std::array<float, 4> boundsMax{};
    };
    static_assert(sizeof(DrawRecord) == 192);

    /** @brief A record translating by (x, y) in clip space, an identity otherwise. */
    DrawRecord RecordAt(float x, float y)
    {
        DrawRecord record;
        record.world = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, x, y, 0.0f, 1.0f};
        record.normal = record.world;
        return record;
    }

    /** @brief The centre pixel of the top-left and the bottom-right quadrant, where records 0 and 1 put their quads. */
    constexpr std::uint32_t Near = Size / 4;
    constexpr std::uint32_t Far = 3 * Size / 4;

    /**
     * @brief Everything an indirect draw of the fixture needs: a pipeline of the indirect fill program, its fill block
     * at set 1 and its draw records at set 3 (record 0 the top-left quadrant, record 1 the bottom-right), a quad's
     * vertices twice over and its indices twice over, so a command may use a first index and base vertex.
     */
    struct IndirectScene
    {
        PipelineHandle pipeline;
        BindingGroupHandle fill;
        BindingGroupHandle draws;
        BufferHandle vertices;
        BufferHandle indices;
        /** @brief The storage layout of set 3, its four blocks as Fenrir/Draw.glsl declares them. */
        BindingGroupLayoutHandle storageLayout;
        BindingGroupLayoutHandle uniformLayout;
    };

    IndirectScene MakeScene(GraphicsDevice& device)
    {
        IndirectScene scene;
        scene.uniformLayout = device.CreateBindingGroupLayout({{{0, Fenrir::ShaderBindingKind::UniformBuffer}}});
        // Fenrir/Draw.glsl declares set 3's four storage blocks whichever a program reads (ADR-0057).
        scene.storageLayout = device.CreateBindingGroupLayout({{{0, Fenrir::ShaderBindingKind::StorageBuffer},
                                                                {1, Fenrir::ShaderBindingKind::StorageBuffer},
                                                                {2, Fenrir::ShaderBindingKind::StorageBuffer},
                                                                {3, Fenrir::ShaderBindingKind::StorageBuffer}}});
        const Fenrir::ShaderContainer container = GlTest::ReadContainer(Fenrir::Shaders::GlIndirectFill);
        PipelineDesc desc;
        desc.shader = &container.variants.at(0);
        desc.sets.at(1) = scene.uniformLayout;
        desc.sets.at(3) = scene.storageLayout;
        desc.vertexBuffers = {{8, {{0, VertexFormat::Float2, 0}}}};
        desc.cull = CullMode::None;
        desc.depth.test = false;
        desc.depth.write = false;
        desc.colourFormats = {TextureFormat::Rgba8Unorm};
        desc.depthFormat.reset();
        const PipelineResult made = device.CreatePipeline(desc, container.name);
        INFO("the indirect fill pipeline is made: " << made.diagnostic);
        REQUIRE_FALSE(made.pipeline.IsEmpty());
        scene.pipeline = made.pipeline;

        const BufferHandle block =
            device.CreateBuffer({sizeof(Fill), BufferUsage::Uniform}, GlTest::BytesOf(Fill), "Fill block");
        BindingGroupEntry fillEntry;
        fillEntry.binding = 0;
        fillEntry.buffer = block;
        scene.fill = device.CreateBindingGroup({scene.uniformLayout, {fillEntry}});

        const std::array<DrawRecord, 2> records{RecordAt(-0.5f, 0.5f), RecordAt(0.5f, -0.5f)};
        const BufferHandle recordBuffer = device.CreateBuffer({sizeof(records), BufferUsage::Storage},
                                                              std::as_bytes(std::span(records)), "Draw records");
        // Fenrir/Draw.glsl declares the instance-list copies and the palette beside the records, which no record here
        // draws from, and the indirection at binding 3: one entry naming no record says these draws are direct, each
        // instance its own record (ADR-0057).
        const BufferHandle copies = device.CreateBuffer({128, BufferUsage::Storage}, {}, "Instance copies");
        const BufferHandle palette = device.CreateBuffer({48, BufferUsage::Storage}, {}, "Palette");
        constexpr std::array<std::uint32_t, 2> noIndirection{0xFFFFFFFFu, 0xFFFFFFFFu};
        const BufferHandle visible = device.CreateBuffer({sizeof(noIndirection), BufferUsage::Storage},
                                                         std::as_bytes(std::span(noIndirection)), "No indirection");
        std::array<BindingGroupEntry, 4> entries;
        for (std::uint32_t binding = 0; binding < entries.size(); ++binding)
        {
            entries.at(binding).binding = binding;
        }
        entries[0].buffer = recordBuffer;
        entries[1].buffer = copies;
        entries[2].buffer = palette;
        entries[3].buffer = visible;
        scene.draws = device.CreateBindingGroup({scene.storageLayout, {entries.begin(), entries.end()}});

        // A quad of side 1 about the origin, twice, so a base vertex of 4 draws the second copy.
        const std::array<float, 16> quad{-0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f,
                                         -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f};
        scene.vertices =
            device.CreateBuffer({sizeof(quad), BufferUsage::Vertex}, std::as_bytes(std::span(quad)), "Quad vertices");
        const std::array<std::uint32_t, 12> quadIndices{0, 1, 2, 0, 2, 3, 0, 1, 2, 0, 2, 3};
        scene.indices = device.CreateBuffer({sizeof(quadIndices), BufferUsage::Index},
                                            std::as_bytes(std::span(quadIndices)), "Quad indices");
        return scene;
    }

    /** @brief A pass clearing `target` to black and declaring `IndirectRead` on `commands`. */
    PassDesc IndirectPass(TextureHandle target, BufferHandle commands)
    {
        PassDesc pass;
        pass.label = "Indirect";
        pass.colour.push_back({target, LoadOp::Clear, Black});
        pass.uses.push_back({target, {}, ResourceUsage::ColourAttachment});
        pass.uses.push_back({{}, commands, ResourceUsage::IndirectRead});
        return pass;
    }

    /** @brief Records the one indirect draw of `commands` in a pass into `target`, and submits it. */
    void DrawIndirect(GraphicsDevice& device, const IndirectScene& scene, TextureHandle target, BufferHandle commands,
                      std::uint32_t drawCount)
    {
        CommandList list;
        {
            PassEncoder encoder(list, IndirectPass(target, commands));
            encoder.SetPipeline(scene.pipeline);
            encoder.SetBindingGroup(1, scene.fill);
            encoder.SetBindingGroup(3, scene.draws);
            encoder.SetVertexBuffer(0, scene.vertices);
            encoder.SetIndexBuffer(scene.indices, IndexFormat::UInt32);
            encoder.DrawIndexedIndirect({commands, 0, drawCount, CommandSize});
        }
        device.Submit(list);
    }
} // namespace

TEST_CASE("One multi-draw of two indirect commands lands both quads at their base instances' records",
          "[renderer][gl][indirect]")
{
    GlTest::GlTestApp test;
    test.RequireOpenGL();
    GraphicsDevice& device = *test.device;
    device.BeginFrame(0);
    const TextureHandle target = test.MakeTarget();
    const IndirectScene scene = MakeScene(device);
    // The second command draws the quad's second copy through the indices' second half, at record 1.
    const std::array<DrawIndexedIndirectCommand, 2> commands{{{6, 1, 0, 0, 0}, {6, 1, 6, 4, 1}}};
    const BufferHandle commandBuffer = device.CreateBuffer({sizeof(commands), BufferUsage::Indirect},
                                                           std::as_bytes(std::span(commands)), "Indirect commands");

    DrawIndirect(device, scene, target, commandBuffer, 2);
    const GlProbe::Image image = GlProbe::Read(device, target);

    INFO("record 0's quad fills the top-left quadrant and record 1's the bottom-right");
    REQUIRE(GlProbe::Near(image.At(Near, Near), Fill));
    REQUIRE(GlProbe::Near(image.At(Far, Far), Fill));
    INFO("the other quadrants are untouched");
    REQUIRE(GlProbe::Near(image.At(Far, Near), Black));
    REQUIRE(GlProbe::Near(image.At(Near, Far), Black));
}

TEST_CASE("An indirect command with no instances draws nothing", "[renderer][gl][indirect]")
{
    GlTest::GlTestApp test;
    test.RequireOpenGL();
    GraphicsDevice& device = *test.device;
    device.BeginFrame(0);
    const TextureHandle target = test.MakeTarget();
    const IndirectScene scene = MakeScene(device);
    const std::array<DrawIndexedIndirectCommand, 2> commands{{{6, 1, 0, 0, 0}, {6, 0, 0, 0, 1}}};
    const BufferHandle commandBuffer = device.CreateBuffer({sizeof(commands), BufferUsage::Indirect},
                                                           std::as_bytes(std::span(commands)), "Indirect commands");

    DrawIndirect(device, scene, target, commandBuffer, 2);
    const GlProbe::Image image = GlProbe::Read(device, target);

    INFO("record 0's quad is drawn");
    REQUIRE(GlProbe::Near(image.At(Near, Near), Fill));
    INFO("record 1's command has no instances, so its quadrant stays black");
    REQUIRE(GlProbe::Near(image.At(Far, Far), Black));
}

TEST_CASE("A compute pass writing instance counts is visible to the indirect draw that follows it",
          "[renderer][gl][indirect]")
{
    GlTest::GlTestApp test;
    test.RequireOpenGL();
    GraphicsDevice& device = *test.device;
    device.BeginFrame(0);
    const TextureHandle target = test.MakeTarget();
    const IndirectScene scene = MakeScene(device);
    // Both commands start with no instances; the compute pass gives the first two (records 0 and 1) and the second
    // none, so what is drawn is what it wrote.
    const std::array<DrawIndexedIndirectCommand, 2> commands{{{6, 0, 0, 0, 0}, {6, 0, 0, 0, 1}}};
    const BufferHandle commandBuffer =
        device.CreateBuffer({sizeof(commands), BufferUsage::Indirect | BufferUsage::Storage},
                            std::as_bytes(std::span(commands)), "Indirect commands");
    const std::array<std::uint32_t, 4> counts{2, 0, 0, 0};
    const BufferHandle job =
        device.CreateBuffer({sizeof(counts), BufferUsage::Uniform}, std::as_bytes(std::span(counts)), "Count job");

    const Fenrir::ShaderContainer container = GlTest::ReadContainer(Fenrir::Shaders::GlIndirectCounts);
    const BindingGroupLayoutHandle commandLayout =
        device.CreateBindingGroupLayout({{{0, Fenrir::ShaderBindingKind::StorageBuffer}}});
    ComputePipelineDesc desc;
    desc.shader = &container.variants.at(0);
    desc.sets.at(0) = scene.uniformLayout;
    desc.sets.at(1) = commandLayout;
    const PipelineResult made = device.CreateComputePipeline(desc, container.name);
    INFO("the count-writing compute pipeline is made: " << made.diagnostic);
    REQUIRE_FALSE(made.pipeline.IsEmpty());
    BindingGroupEntry jobEntry;
    jobEntry.binding = 0;
    jobEntry.buffer = job;
    const BindingGroupHandle jobGroup = device.CreateBindingGroup({scene.uniformLayout, {jobEntry}});
    BindingGroupEntry commandEntry;
    commandEntry.binding = 0;
    commandEntry.buffer = commandBuffer;
    const BindingGroupHandle commandGroup = device.CreateBindingGroup({commandLayout, {commandEntry}});

    CommandList list;
    {
        PassDesc pass;
        pass.label = "Counts";
        pass.queue = QueueKind::Compute;
        pass.uses.push_back({{}, commandBuffer, ResourceUsage::StorageWrite});
        PassEncoder encoder(list, pass);
        encoder.SetPipeline(made.pipeline);
        encoder.SetBindingGroup(0, jobGroup);
        encoder.SetBindingGroup(1, commandGroup);
        encoder.Dispatch({2, 1, 1});
    }
    device.Submit(list);
    DrawIndirect(device, scene, target, commandBuffer, 2);
    const GlProbe::Image image = GlProbe::Read(device, target);

    INFO("the first command's two instances draw records 0 and 1, both quadrants");
    REQUIRE(GlProbe::Near(image.At(Near, Near), Fill));
    REQUIRE(GlProbe::Near(image.At(Far, Far), Fill));
    INFO("the quadrants no record covers stay black");
    REQUIRE(GlProbe::Near(image.At(Far, Near), Black));
    REQUIRE(GlProbe::Near(image.At(Near, Far), Black));
}
