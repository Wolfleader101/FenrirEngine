// Debug drawing on OpenGL (the spec's seam 2, ADR-0025): thick lines expanded in the vertex shader, depth tested
// against the view's SceneDepth or drawn on top, in one view and in several, and debug text, read back from each view's
// SceneColor.

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "FenrirDebugDraw/DebugDrawing.hpp"
#include "GlScene.hpp"

using Fenrir::GlProbe;
using GlTest::GlScene;

namespace
{
    constexpr std::uint32_t Size = GlScene::Size;
    constexpr std::uint32_t Centre = Size / 2;
    constexpr float Forever = Fenrir::DebugDraw::Forever;

    const GlProbe::Pixel Red{1.0f, 0.0f, 0.0f, 1.0f};
    const GlProbe::Pixel Green{0.0f, 1.0f, 0.0f, 1.0f};

    Fenrir::DebugDraw& Draw(GlScene& test)
    {
        Fenrir::DebugDraw* draw = test.app.GetActiveScene().GetResource<Fenrir::DebugDraw>();
        REQUIRE(draw != nullptr);
        return *draw;
    }

    /**
     * @brief A green vertical line through the view's centre, `thickness` pixels across, three units ahead: behind the
     * red quad the tests put two units ahead, which covers the middle of the view but not its top.
     */
    void AddLineBehindQuad(GlScene& test, Fenrir::DepthMode depth, float thickness = 6.0f)
    {
        Draw(test).Line({0.0f, -5.0f, -3.0f}, {0.0f, 5.0f, -3.0f}, Fenrir::Colour::Green,
                        {.duration = Forever, .thickness = thickness, .depth = depth});
    }

    GlProbe::Image Read(GlScene& test, std::uint32_t width, std::uint32_t height)
    {
        Fenrir::Graphics::GraphicsDevice& device = Fenrir::RendererProbe::Device(test.renderer);
        const Fenrir::Graphics::TextureHandle sceneColour =
            Fenrir::RendererProbe::SceneColour(test.renderer, width, height);
        REQUIRE_FALSE(sceneColour.IsEmpty());
        return GlProbe::Read(device, sceneColour);
    }
} // namespace

TEST_CASE("A thick on-top debug line covers an opaque quad in front of it; a depth-tested one is hidden by it",
          "[renderer][gl][debug]")
{
    for (const Fenrir::DepthMode depth : {Fenrir::DepthMode::OnTop, Fenrir::DepthMode::Tested})
    {
        GlScene test;
        test.RequireOpenGL();
        Fenrir::DebugDrawing debugDrawing = Fenrir::DebugDrawing::Install(test.app);
        test.AddCamera();
        test.AddQuad(1.0f, 2.0f, "builtin:unlit", Fenrir::Colour::Red);
        AddLineBehindQuad(test, depth);

        const GlProbe::Image image = test.Frame();

        const bool onTop = depth == Fenrir::DepthMode::OnTop;
        INFO((onTop ? "on top" : "depth tested") << ": above the quad the line shows, green");
        REQUIRE(GlProbe::Near(image.At(Centre, 4), Green));
        INFO("its thickness is pixels across: two pixels either side of its centre are green too");
        REQUIRE(GlProbe::Near(image.At(Centre - 2, 4), Green));
        REQUIRE(GlProbe::Near(image.At(Centre + 2, 4), Green));
        INFO("and eight pixels to the side, the sky shows");
        REQUIRE_FALSE(GlProbe::Near(image.At(Centre + 8, 4), Green));
        INFO("over the quad, the on-top line shows and the depth-tested one is hidden");
        REQUIRE(GlProbe::Near(image.At(Centre, Centre), onTop ? Green : Red));
        REQUIRE(test.messages->errors.empty());
    }
}

TEST_CASE("Depth-tested debug lines are hidden by the quad in front of them in every view", "[renderer][gl][debug]")
{
    GlScene test;
    test.RequireOpenGL();
    Fenrir::DebugDrawing debugDrawing = Fenrir::DebugDrawing::Install(test.app);
    // Two views side by side, a quarter and three quarters of the window, so each SceneColor has its own size.
    test.AddCamera().viewport = {0.0f, 0.0f, 0.25f, 1.0f};
    test.AddCamera().viewport = {0.25f, 0.0f, 0.75f, 1.0f};
    test.AddQuad(1.0f, 2.0f, "builtin:unlit", Fenrir::Colour::Red);
    AddLineBehindQuad(test, Fenrir::DepthMode::Tested, 2.0f);

    test.RunFrame();

    for (const std::uint32_t width : {Size / 4, Size * 3 / 4})
    {
        const GlProbe::Image image = Read(test, width, Size);
        INFO("in the view " << width << " pixels wide, the line shows above the quad");
        REQUIRE(GlProbe::Near(image.At(width / 2, 4), Green));
        INFO("and the quad hides it in the middle");
        REQUIRE(GlProbe::Near(image.At(width / 2, Centre), Red));
    }
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("World text draws over the scene, and screen text and messages draw with no GL error",
          "[renderer][gl][debug][text]")
{
    GlScene test;
    test.RequireOpenGL();
    Fenrir::DebugDrawing debugDrawing = Fenrir::DebugDrawing::Install(test.app);
    test.AddCamera();
    test.AddQuad(1.0f, 2.0f, "builtin:unlit", Fenrir::Colour::Red);
    Fenrir::DebugDraw& draw = Draw(test);
    // Behind the quad, but world text is never occluded.
    draw.WorldText({0.0f, 0.0f, -3.0f}, "#", Fenrir::Colour::White, {.duration = Forever, .size = 32.0f});
    draw.ScreenText({4.0f, 4.0f}, "Fenrir", Fenrir::Colour::White, {.duration = Forever});
    draw.Message(1, "a message", Fenrir::Colour::Yellow, {.duration = Forever});

    const GlProbe::Image image = test.Frame();

    bool white = false;
    for (std::uint32_t y = Centre - 10; y < Centre + 10; ++y)
    {
        for (std::uint32_t x = Centre - 10; x < Centre + 10; ++x)
        {
            const GlProbe::Pixel pixel = image.At(x, y);
            white = white || (pixel[1] > 0.9f && pixel[2] > 0.9f);
        }
    }
    INFO("the '#' shows white over the red quad round the centre");
    REQUIRE(white);
    INFO("the Overlay pass's screen text and message raise no GL error");
    REQUIRE(test.messages->errors.empty());
}
