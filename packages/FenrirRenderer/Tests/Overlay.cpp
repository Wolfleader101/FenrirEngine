// The window overlay through the ECS facade (ADR-0027): 2D draw lists submitted to the Overlay resource, drawn in the
// window's `Overlay 2D` pass on the null backend, read back through RendererProbe (the spec's seam 1).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "DrawLists2D.hpp"
#include "FenrirDebugDraw/DebugDrawing.hpp"
#include "FenrirRenderer/Overlay.hpp"
#include "FenrirShaders/Tinted2D.hpp"
#include "RendererTestApp.hpp"

using Fenrir::RendererProbe;
using RendererTest::AddQuad;
using RendererTest::Pipeline2D;
using RendererTest::RendererTestApp;

namespace
{
    /** @brief The main window, the one the renderer draws: a WindowTarget's default. */
    constexpr Fenrir::WindowId MainWindow = Fenrir::WindowTarget{}.window;

    Fenrir::Overlay& OverlayOf(RendererTestApp& test)
    {
        Fenrir::Overlay* overlay = test.app.TryGetResource<Fenrir::Overlay>();
        INFO("Renderer::Install adds the Overlay app resource");
        REQUIRE(overlay != nullptr);
        return *overlay;
    }

    /** @brief A list of `quads` quads on builtin:2d, with one command for each of `drawn`, by quad. */
    Fenrir::DrawList2D Quads(RendererTestApp& test, std::uint32_t quads, const std::vector<std::uint32_t>& drawn)
    {
        Fenrir::DrawList2D list;
        const std::uint32_t pipeline = list.AddPipeline(Pipeline2D(test.Builtin("builtin:2d")));
        for (std::uint32_t q = 0; q < quads; ++q)
        {
            AddQuad(list, pipeline, {0.0f, 0.0f}, {10.0f, 10.0f});
        }
        std::vector<Fenrir::Command2D> commands;
        for (const std::uint32_t q : drawn)
        {
            commands.push_back(list.commands.at(q));
        }
        list.commands = commands;
        return list;
    }

    /** @brief The first index of each draw of the last frame's `Overlay 2D` pass, in order. */
    std::vector<std::uint32_t> FirstIndices(const RendererTestApp& test)
    {
        std::vector<std::uint32_t> firsts;
        for (const RendererTest::RecordedDraw& draw : test.Draws("Overlay 2D"))
        {
            firsts.push_back(draw.draw.firstIndex);
        }
        return firsts;
    }
} // namespace

TEST_CASE("Overlay layers draw lowest first, then in submission and command order, all before debug screen text",
          "[renderer][overlay]")
{
    RendererTestApp test;
    Fenrir::DebugDrawing debugDrawing = Fenrir::DebugDrawing::Install(test.app);
    test.AddCamera();
    test.ActiveScene().GetResource<Fenrir::DebugDraw>()->ScreenText({4.0f, 4.0f}, "top", Fenrir::Colour::White,
                                                                    {.duration = Fenrir::DebugDraw::Forever});
    Fenrir::Overlay& overlay = OverlayOf(test);
    // Each command draws a quad of its own, so its first index names it: 6 indices a quad.
    overlay.Submit(MainWindow, 1, Quads(test, 2, {0, 1}));
    overlay.Submit(MainWindow, 0, Quads(test, 3, {2}));
    overlay.Submit(MainWindow, 1, Quads(test, 4, {3}));

    test.Frame();
    const std::vector<std::string> passes = RendererTest::PassesIn(RendererProbe::Submitted(test.renderer));

    INFO("layer 0's command, then layer 1's two submissions in the order they were made, each in command order");
    REQUIRE(FirstIndices(test) == std::vector<std::uint32_t>{12, 0, 6, 18});
    INFO("one Overlay 2D pass, then the debug feature's Overlay pass with the screen text, last of all");
    REQUIRE(std::count(passes.begin(), passes.end(), "Overlay 2D") == 1);
    REQUIRE(passes.size() >= 2);
    REQUIRE(passes[passes.size() - 2] == "Overlay 2D");
    REQUIRE(passes.back() == "Overlay");
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A 2D draw list binds the 2D target at set 0 and its vertices, indices and scissor", "[renderer][overlay]")
{
    RendererTestApp test;
    test.AddCamera();
    OverlayOf(test).Submit(MainWindow, 0, Quads(test, 1, {0}));

    test.Frame();
    const std::vector<RendererTest::RecordedDraw> draws = test.Draws("Overlay 2D");

    INFO("one draw of the quad's six indices with builtin:2d's pipeline");
    REQUIRE(draws.size() == 1);
    REQUIRE(draws[0].draw.indexCount == 6);
    REQUIRE(RendererProbe::Label(test.Device(), draws[0].pipeline) == "Builtin2D");
    INFO("with the 2D target block at set 0 and the command's texture at set 1");
    REQUIRE(test.GroupLabel(draws[0], 0) == "2D target");
    REQUIRE(test.GroupLabel(draws[0], 1) == "2D command");
    INFO("from the frame's vertex and index buffers");
    REQUIRE_FALSE(draws[0].vertexBuffers.at(0).IsEmpty());
    REQUIRE_FALSE(draws[0].indexBuffer.IsEmpty());
    const RendererProbe::PipelineState state = RendererProbe::State(test.Device(), draws[0].pipeline);
    INFO("straight alpha, no depth and no culling, encoding linear values into the sRGB backbuffer");
    REQUIRE(state.blend == Fenrir::Graphics::BlendMode::Alpha);
    REQUIRE_FALSE(state.depth.test);
    REQUIRE(state.cull == Fenrir::Graphics::CullMode::None);
    REQUIRE(state.encodeSrgb);
    INFO("the sRGB input colour space picks the INPUT_SRGB variant");
    REQUIRE(RendererProbe::Keys(test.Device(), draws[0].pipeline) == std::vector<std::string>{"INPUT_SRGB"});
    REQUIRE(RendererProbe::StaleUses(test.Device()).empty());
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Submissions are cleared after Render, and a frame with none adds no Overlay 2D pass", "[renderer][overlay]")
{
    RendererTestApp test;
    test.AddCamera();
    OverlayOf(test).Submit(MainWindow, 0, Quads(test, 1, {0}));

    test.Frame();
    INFO("the first frame draws the submission");
    REQUIRE(test.Draws("Overlay 2D").size() == 1);

    test.Frame();
    const std::vector<std::string> passes = RendererTest::PassesIn(RendererProbe::Submitted(test.renderer));
    INFO("the next frame, with nothing submitted, draws no UI");
    REQUIRE(std::count(passes.begin(), passes.end(), "Overlay 2D") == 0);
}

TEST_CASE("The overlay draws over a window no camera draws into", "[renderer][overlay]")
{
    RendererTestApp test;
    OverlayOf(test).Submit(MainWindow, 0, Quads(test, 1, {0}));

    test.Frame();

    INFO("the window is cleared to black, and the overlay still drawn over it");
    REQUIRE(RendererTest::PassesIn(RendererProbe::Submitted(test.renderer)) ==
            std::vector<std::string>{"BrdfLut", "Clear", "Overlay 2D"});
    REQUIRE(test.Draws("Overlay 2D").size() == 1);
}

TEST_CASE("A submission to a window the renderer does not draw is dropped, logged once", "[renderer][overlay]")
{
    RendererTestApp test;
    test.AddCamera();
    const Fenrir::WindowId other{MainWindow.value + 1};

    for (int frame = 0; frame < 2; ++frame)
    {
        OverlayOf(test).Submit(other, 0, Quads(test, 1, {0}));
        test.Frame();
        INFO("frame " << frame << " draws nothing for the other window");
        REQUIRE(test.Draws("Overlay 2D").empty());
    }

    INFO("the dropped submissions were logged once, naming the window");
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "does not draw") == 1);
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "window " + std::to_string(other.value)) == 1);
}

TEST_CASE("Equal 2D pipeline descriptions share one pipeline, across lists and frames", "[renderer][overlay]")
{
    RendererTestApp test;
    test.AddCamera();
    Fenrir::Overlay& overlay = OverlayOf(test);
    overlay.Submit(MainWindow, 0, Quads(test, 1, {0}));
    overlay.Submit(MainWindow, 0, Quads(test, 1, {0}));
    test.Frame();
    const std::size_t pipelines = RendererProbe::LivePipelines(test.Device());

    overlay.Submit(MainWindow, 0, Quads(test, 1, {0}));
    test.Frame();
    const std::vector<RendererTest::RecordedDraw> draws = test.Draws("Overlay 2D");

    INFO("a later frame's description, equal by value, made no pipeline of its own");
    REQUIRE(draws.size() == 1);
    REQUIRE(RendererProbe::LivePipelines(test.Device()) == pipelines);
    INFO("a description that differs makes a pipeline of its own");
    Fenrir::DrawList2D premultiplied = Quads(test, 1, {0});
    premultiplied.pipelines.at(0).blendMode = Fenrir::BlendMode2D::PremultipliedAlpha;
    overlay.Submit(MainWindow, 0, std::move(premultiplied));
    test.Frame();
    REQUIRE(RendererProbe::LivePipelines(test.Device()) == pipelines + 1);
}

TEST_CASE("Two lists in one frame with equal descriptions draw with the same pipeline", "[renderer][overlay]")
{
    RendererTestApp test;
    test.AddCamera();
    Fenrir::Overlay& overlay = OverlayOf(test);
    overlay.Submit(MainWindow, 0, Quads(test, 1, {0}));
    overlay.Submit(MainWindow, 0, Quads(test, 1, {0}));

    test.Frame();
    const std::vector<RendererTest::RecordedDraw> draws = test.Draws("Overlay 2D");

    INFO("each list's draw binds the one pipeline");
    REQUIRE(draws.size() == 2);
    REQUIRE(draws[0].pipeline == draws[1].pipeline);
}

TEST_CASE("A Gamma pipeline on the window's sRGB backbuffer draws with encoding off and the GAMMA variant",
          "[renderer][overlay]")
{
    RendererTestApp test;
    test.AddCamera();
    Fenrir::DrawList2D list = Quads(test, 1, {0});
    list.pipelines.at(0).blendSpace = Fenrir::BlendSpace2D::Gamma;
    list.pipelines.at(0).blendMode = Fenrir::BlendMode2D::PremultipliedAlpha;
    OverlayOf(test).Submit(MainWindow, 0, std::move(list));

    test.Frame();
    const std::vector<RendererTest::RecordedDraw> draws = test.Draws("Overlay 2D");

    INFO("the backbuffer is 8-bit sRGB, so the gamma path is allowed: no error");
    REQUIRE(draws.size() == 1);
    REQUIRE(test.messages->errors.empty());
    INFO("its values pass through unencoded, blended premultiplied, from the variant with every key it picked");
    const RendererProbe::PipelineState state = RendererProbe::State(test.Device(), draws[0].pipeline);
    REQUIRE_FALSE(state.encodeSrgb);
    REQUIRE(state.blend == Fenrir::Graphics::BlendMode::Premultiplied);
    REQUIRE(RendererProbe::Keys(test.Device(), draws[0].pipeline) ==
            std::vector<std::string>{"INPUT_SRGB", "PREMULTIPLY", "GAMMA"});
}

TEST_CASE("A 2D draw list naming a stale shader is skipped, logged once", "[renderer][overlay]")
{
    RendererTestApp test;
    test.AddCamera();
    Fenrir::DrawList2D list;
    const std::uint32_t pipeline = list.AddPipeline(Pipeline2D(Fenrir::AssetHandle<Fenrir::Shader>{77, 3}));
    AddQuad(list, pipeline, {0.0f, 0.0f}, {8.0f, 8.0f});

    for (int frame = 0; frame < 2; ++frame)
    {
        OverlayOf(test).Submit(MainWindow, 0, list);
        test.Frame();
        INFO("frame " << frame << " draws nothing");
        REQUIRE(test.Draws("Overlay 2D").empty());
    }
    INFO("one error for the stale shader");
    REQUIRE(RendererTest::CountContaining(test.messages->errors, "stale shader") == 1);
}

TEST_CASE("A command's parameter bytes are written over the start of its shader's block, the rest kept at defaults",
          "[renderer][overlay]")
{
    RendererTestApp test;
    test.AddCamera();
    Fenrir::ShaderData shader;
    const auto bytes = std::as_bytes(std::span(Fenrir::Shaders::Tinted2D));
    shader.bytes.assign(bytes.begin(), bytes.end());
    const auto tinted = test.Store().Add<Fenrir::Shader>(std::move(shader), Fenrir::AssetOwner::App());
    Fenrir::DrawList2D list;
    const std::uint32_t pipeline = list.AddPipeline(Pipeline2D(tinted));
    // The block's tint, a vec4 at offset 0; its strength, at offset 16, keeps its default 0.5.
    const std::array<float, 4> tint{0.25f, 0.5f, 0.75f, 1.0f};
    const auto tintBytes = std::as_bytes(std::span(tint));
    AddQuad(list, pipeline, {0.0f, 0.0f}, {8.0f, 8.0f}).parameters.assign(tintBytes.begin(), tintBytes.end());
    OverlayOf(test).Submit(MainWindow, 0, std::move(list));

    test.Frame();
    const std::vector<RendererTest::RecordedDraw> draws = test.Draws("Overlay 2D");
    REQUIRE(draws.size() == 1);
    std::vector<std::byte> block;
    for (const Fenrir::Graphics::BindingGroupEntry& entry :
         RendererProbe::Entries(test.Device(), draws[0].groups.at(1)))
    {
        if (entry.binding == 0)
        {
            const std::vector<std::byte> buffer = RendererProbe::Bytes(test.Device(), entry.buffer);
            REQUIRE(entry.offset + entry.size <= buffer.size());
            block.assign(buffer.begin() + static_cast<std::ptrdiff_t>(entry.offset),
                         buffer.begin() + static_cast<std::ptrdiff_t>(entry.offset + entry.size));
        }
    }

    INFO("the command's tint is the block's first sixteen bytes");
    REQUIRE(RendererTest::FloatAt(block, 0) == 0.25f);
    REQUIRE(RendererTest::FloatAt(block, 4) == 0.5f);
    REQUIRE(RendererTest::FloatAt(block, 8) == 0.75f);
    REQUIRE(RendererTest::FloatAt(block, 12) == 1.0f);
    INFO("the strength the command does not reach keeps its parameter default");
    REQUIRE(RendererTest::FloatAt(block, 16) == 0.5f);
    REQUIRE(test.messages->errors.empty());
}
