// The renderer drawing a scene's DebugDraw (ADR-0025, ADR-0026): shapes and world text in each view's Debug pass, and
// screen text and on-screen messages in the window's Overlay pass, on the null backend through RendererProbe (the
// spec's seam 1).

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirDebugDraw/DebugDrawing.hpp"
#include "RendererTestApp.hpp"

using Fenrir::RendererProbe;
using RendererTest::RendererTestApp;

namespace
{
    constexpr float Forever = Fenrir::DebugDraw::Forever;

    /** @brief One non-indexed draw recorded in a pass, with the pipeline it drew with. */
    struct PlainDraw
    {
        Fenrir::Graphics::PipelineHandle pipeline;
        Fenrir::Graphics::CmdDraw draw;
    };

    /** @brief One pass of the last frame: its label and its non-indexed draws. */
    struct PassDraws
    {
        std::string label;
        std::vector<PlainDraw> draws;
    };

    /** @brief Every pass of the last frame, in order, with its non-indexed draws. */
    std::vector<PassDraws> Passes(const RendererTestApp& test)
    {
        using namespace Fenrir::Graphics;
        std::vector<PassDraws> passes;
        PipelineHandle pipeline;
        for (const CommandList& list : RendererProbe::Submitted(test.renderer))
        {
            for (const Command& command : list.commands)
            {
                if (const auto* begin = std::get_if<CmdBeginPass>(&command))
                {
                    passes.push_back({begin->pass.label, {}});
                }
                else if (const auto* set = std::get_if<CmdSetPipeline>(&command))
                {
                    pipeline = set->pipeline;
                }
                else if (const auto* draw = std::get_if<CmdDraw>(&command); draw != nullptr && !passes.empty())
                {
                    passes.back().draws.push_back({pipeline, *draw});
                }
            }
        }
        return passes;
    }

    std::vector<PassDraws> Labelled(const std::vector<PassDraws>& passes, const std::string& label)
    {
        std::vector<PassDraws> found;
        for (const PassDraws& pass : passes)
        {
            if (pass.label == label)
            {
                found.push_back(pass);
            }
        }
        return found;
    }

    /** @brief The draws of `pass` whose pipeline was made from the built-in program `program`. */
    std::vector<PlainDraw> DrawsWith(const RendererTestApp& test, const PassDraws& pass, const std::string& program)
    {
        std::vector<PlainDraw> found;
        for (const PlainDraw& draw : pass.draws)
        {
            if (RendererProbe::Label(test.Device(), draw.pipeline) == program)
            {
                found.push_back(draw);
            }
        }
        return found;
    }

    bool DepthTested(const RendererTestApp& test, const PlainDraw& draw)
    {
        return RendererProbe::State(test.Device(), draw.pipeline).depth.test;
    }

    /** @brief A camera at `x` along the X axis, five units back, drawing into the window's part `viewport`. */
    Fenrir::Camera& AddCamera(RendererTestApp& test, float x, Fenrir::Viewport viewport)
    {
        Fenrir::Camera& camera = test.AddCamera({x, 0.0f, 5.0f}).GetComponent<Fenrir::Camera>();
        camera.viewport = viewport;
        return camera;
    }

    Fenrir::DebugDraw& Draw(RendererTestApp& test)
    {
        Fenrir::DebugDraw* draw = test.ActiveScene().GetResource<Fenrir::DebugDraw>();
        REQUIRE(draw != nullptr);
        return *draw;
    }
} // namespace

TEST_CASE("Debug lines and solids draw in each view's Debug pass, with the line and triangle pipelines",
          "[renderer][debug]")
{
    RendererTestApp test;
    Fenrir::DebugDrawing debugDrawing = Fenrir::DebugDrawing::Install(test.app);
    AddCamera(test, -1.0f, {0.0f, 0.0f, 0.5f, 1.0f});
    AddCamera(test, 1.0f, {0.5f, 0.0f, 0.5f, 1.0f});
    Fenrir::DebugDraw& draw = Draw(test);
    draw.Line({0, 0, 0}, {1, 0, 0}, Fenrir::Colour::Red, {.duration = Forever, .thickness = 3.0f});
    draw.Line({0, 1, 0}, {1, 1, 0}, Fenrir::Colour::Red, {.duration = Forever});
    draw.Triangles(std::vector<Fenrir::Math::Vec3>{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, Fenrir::Colour::Blue,
                   {.duration = Forever});

    test.Frame();
    const std::vector<PassDraws> passes = Passes(test);
    const std::vector<PassDraws> debug = Labelled(passes, "Debug");

    INFO("each of the two views has a Debug pass");
    REQUIRE(debug.size() == 2);
    for (const PassDraws& pass : debug)
    {
        const std::vector<PlainDraw> lines = DrawsWith(test, pass, "BuiltinDebugLines");
        const std::vector<PlainDraw> triangles = DrawsWith(test, pass, "BuiltinDebugTriangles");
        INFO("both lines are one draw of six vertices a line, depth tested");
        REQUIRE(lines.size() == 1);
        REQUIRE(lines[0].draw.vertexCount == 12);
        REQUIRE(DepthTested(test, lines[0]));
        INFO("the triangle is one draw of three vertices, depth tested");
        REQUIRE(triangles.size() == 1);
        REQUIRE(triangles[0].draw.vertexCount == 3);
        REQUIRE(DepthTested(test, triangles[0]));
    }
    INFO("each Debug pass runs before its view's Output, which then shows it");
    std::size_t debugsSeen = 0;
    std::size_t outputsSeen = 0;
    for (const PassDraws& pass : passes)
    {
        debugsSeen += pass.label == "Debug" ? 1 : 0;
        if (pass.label == "Output")
        {
            ++outputsSeen;
            REQUIRE(debugsSeen >= outputsSeen);
        }
    }
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("On-top debug shapes draw with the pipelines that do not test depth, after the depth-tested ones",
          "[renderer][debug]")
{
    RendererTestApp test;
    Fenrir::DebugDrawing debugDrawing = Fenrir::DebugDrawing::Install(test.app);
    test.AddCamera();
    Fenrir::DebugDraw& draw = Draw(test);
    draw.Line({0, 0, 0}, {1, 0, 0}, Fenrir::Colour::Red, {.duration = Forever, .depth = Fenrir::DepthMode::OnTop});
    draw.Line({0, 1, 0}, {1, 1, 0}, Fenrir::Colour::Red, {.duration = Forever});
    draw.SolidBox(Fenrir::Math::AABB{{0, 0, 0}, {1, 1, 1}}, Fenrir::Colour::Blue,
                  {.duration = Forever, .depth = Fenrir::DepthMode::OnTop});

    test.Frame();
    const std::vector<PassDraws> debug = Labelled(Passes(test), "Debug");

    REQUIRE(debug.size() == 1);
    const std::vector<PlainDraw> lines = DrawsWith(test, debug[0], "BuiltinDebugLines");
    const std::vector<PlainDraw> triangles = DrawsWith(test, debug[0], "BuiltinDebugTriangles");
    INFO("the tested line, then the on-top line, each its own draw");
    REQUIRE(lines.size() == 2);
    REQUIRE(DepthTested(test, lines[0]));
    REQUIRE_FALSE(DepthTested(test, lines[1]));
    REQUIRE(lines[1].draw.vertexCount == 6);
    INFO("the on-top box's twelve triangles draw without the depth test");
    REQUIRE(triangles.size() == 1);
    REQUIRE_FALSE(DepthTested(test, triangles[0]));
    REQUIRE(triangles[0].draw.vertexCount == 36);
    INFO("no pipeline that draws debug shapes writes depth");
    for (const PlainDraw& d : debug[0].draws)
    {
        REQUIRE_FALSE(RendererProbe::State(test.Device(), d.pipeline).depth.write);
    }
}

TEST_CASE("A camera with drawDebug off draws no debug drawing, world text included, and other views still do",
          "[renderer][debug][cameras]")
{
    RendererTestApp test;
    Fenrir::DebugDrawing debugDrawing = Fenrir::DebugDrawing::Install(test.app);
    AddCamera(test, 0.0f, {0.0f, 0.0f, 0.5f, 1.0f}).drawDebug = false;
    AddCamera(test, 0.0f, {0.5f, 0.0f, 0.5f, 1.0f});
    Fenrir::DebugDraw& draw = Draw(test);
    draw.Line({0, 0, 0}, {1, 0, 0}, Fenrir::Colour::Red, {.duration = Forever});
    draw.WorldText({0, 0, 0}, "here", Fenrir::Colour::White, {.duration = Forever});

    test.Frame();
    const std::vector<PassDraws> debug = Labelled(Passes(test), "Debug");

    INFO("only the second view has a Debug pass");
    REQUIRE(debug.size() == 1);
    REQUIRE(DrawsWith(test, debug[0], "BuiltinDebugLines").size() == 1);
    REQUIRE(DrawsWith(test, debug[0], "BuiltinDebugText").size() == 1);
    INFO("both views still draw the scene");
    REQUIRE(Labelled(Passes(test), "Output").size() == 2);
}

TEST_CASE("Screen text and messages draw once, in the window's Overlay pass, after every other pass",
          "[renderer][debug][overlay]")
{
    RendererTestApp test;
    Fenrir::DebugDrawing debugDrawing = Fenrir::DebugDrawing::Install(test.app);
    AddCamera(test, -1.0f, {0.0f, 0.0f, 0.5f, 1.0f});
    AddCamera(test, 1.0f, {0.5f, 0.0f, 0.5f, 1.0f});
    Fenrir::DebugDraw& draw = Draw(test);
    draw.ScreenText({10.0f, 10.0f}, "abc", Fenrir::Colour::White, {.duration = Forever});
    draw.Message(7, "de", Fenrir::Colour::Yellow, {.duration = Forever});

    test.Frame();
    const std::vector<PassDraws> passes = Passes(test);
    const std::vector<PassDraws> overlay = Labelled(passes, "Overlay");

    INFO("one Overlay pass for the window, though two views draw into it");
    REQUIRE(overlay.size() == 1);
    REQUIRE(passes.back().label == "Overlay");
    const std::vector<PlainDraw> text = DrawsWith(test, overlay[0], "BuiltinDebugText");
    INFO("one draw of the five glyphs, six vertices each");
    REQUIRE(text.size() == 1);
    REQUIRE(text[0].draw.vertexCount == 30);
    INFO("the views' Debug passes hold no screen text: they have nothing to draw, so there are none");
    REQUIRE(Labelled(passes, "Debug").empty());
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A view into an image gets no screen text", "[renderer][debug][overlay][cameras]")
{
    RendererTestApp test;
    Fenrir::DebugDrawing debugDrawing = Fenrir::DebugDrawing::Install(test.app);
    test.AddCamera().GetComponent<Fenrir::Camera>().target = Fenrir::ImageTarget{test.TargetImage(32, 32)};
    Draw(test).ScreenText({10.0f, 10.0f}, "abc", Fenrir::Colour::White, {.duration = Forever});
    Draw(test).Message("de", Fenrir::Colour::White, {.duration = Forever});

    test.Frame();
    const std::vector<PassDraws> passes = Passes(test);

    INFO("the image's view is drawn, but no Overlay pass draws the text, there or in the window");
    REQUIRE(Labelled(passes, "Output").size() == 1);
    REQUIRE(Labelled(passes, "Overlay").empty());
    REQUIRE(Labelled(passes, "Debug").empty());
}

TEST_CASE("World text draws in the view's Debug pass, and is skipped behind the camera", "[renderer][debug]")
{
    RendererTestApp test;
    Fenrir::DebugDrawing debugDrawing = Fenrir::DebugDrawing::Install(test.app);
    // At z 5, looking along -Z.
    test.AddCamera();
    Fenrir::DebugDraw& draw = Draw(test);
    draw.WorldText({0, 0, 0}, "ahead", Fenrir::Colour::White, {.duration = Forever});
    draw.WorldText({0, 0, 10}, "behind", Fenrir::Colour::White, {.duration = Forever});

    test.Frame();
    const std::vector<PassDraws> debug = Labelled(Passes(test), "Debug");

    REQUIRE(debug.size() == 1);
    const std::vector<PlainDraw> text = DrawsWith(test, debug[0], "BuiltinDebugText");
    INFO("one draw of 'ahead', five glyphs of six vertices; 'behind' adds none");
    REQUIRE(text.size() == 1);
    REQUIRE(text[0].draw.vertexCount == 30);
    INFO("world text is never occluded: its pipeline does not test depth");
    REQUIRE_FALSE(DepthTested(test, text[0]));
}

TEST_CASE("A scene with nothing in its DebugDraw adds no Debug or Overlay pass", "[renderer][debug]")
{
    RendererTestApp test;
    Fenrir::DebugDrawing debugDrawing = Fenrir::DebugDrawing::Install(test.app);
    test.AddCamera();

    test.Frame();
    const std::vector<PassDraws> passes = Passes(test);

    REQUIRE(Labelled(passes, "Debug").empty());
    REQUIRE(Labelled(passes, "Overlay").empty());
}
