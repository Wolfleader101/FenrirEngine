// The painter's shader and pipeline on the renderer's null backend (the spec's seam 4): Painting::Install registers
// builtin:painter, and a painted list submitted to the window overlay makes one device pipeline, cached by value, from
// the variant of its blend space. Read back through RendererProbe, as the renderer's own overlay tests are.

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirPainter/Painter.hpp"
#include "FenrirPainter/Painting.hpp"
#include "FenrirRenderer/Overlay.hpp"
#include "RendererTestApp.hpp"

using Fenrir::RendererProbe;
using RendererTest::RendererTestApp;

namespace
{
    constexpr Fenrir::WindowId MainWindow = Fenrir::WindowTarget{}.window;

    /** @brief A list painted with builtin:painter in `blendSpace`: a rounded rectangle and a gradient circle. */
    Fenrir::DrawList2D Painted(const Fenrir::Painting& painting, Fenrir::BlendSpace2D blendSpace)
    {
        Fenrir::DrawList2D list;
        Fenrir::PainterConfig config;
        config.shader = painting.GetShader();
        config.blendSpace = blendSpace;
        Fenrir::Painter painter(list, config);
        painter.DrawRoundedRect({10, 10, 100, 40}, Fenrir::CornerRadii::All(8), Fenrir::Colour::Red);
        Fenrir::LinearGradient gradient;
        gradient.end = {40, 0};
        gradient.stops = {{0.0f, Fenrir::Colour::White}, {1.0f, Fenrir::Colour::Blue}};
        painter.DrawCircle({60, 80}, 20, gradient);
        return list;
    }

    Fenrir::Overlay& OverlayOf(RendererTestApp& test)
    {
        Fenrir::Overlay* overlay = test.app.TryGetResource<Fenrir::Overlay>();
        INFO("Renderer::Install adds the Overlay app resource");
        REQUIRE(overlay != nullptr);
        return *overlay;
    }
} // namespace

TEST_CASE("Painting::Install registers builtin:painter with the asset store", "[painter][pipelines]")
{
    RendererTestApp test;
    const Fenrir::Painting painting = Fenrir::Painting::Install(test.app);

    INFO("the install is valid and its shader is the one the name loads");
    REQUIRE(painting.IsValid());
    REQUIRE(test.Builtin("builtin:painter") == painting.GetShader());
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Painting::Install with no asset store logs why and is not valid", "[painter][pipelines]")
{
    auto messages = std::make_shared<RendererTest::LoggedMessages>();
    Fenrir::App app{std::make_unique<RendererTest::RecordingLogger>(messages), 1};

    const Fenrir::Painting painting = Fenrir::Painting::Install(app);

    INFO("no shader handle, and one error naming the asset store");
    REQUIRE_FALSE(painting.IsValid());
    REQUIRE(painting.GetShader().IsEmpty());
    REQUIRE(RendererTest::CountContaining(messages->errors, "asset store") == 1);
}

TEST_CASE("The painter's pipeline description makes one device pipeline, cached by value across lists and frames",
          "[painter][pipelines]")
{
    RendererTestApp test;
    const Fenrir::Painting painting = Fenrir::Painting::Install(test.app);
    test.AddCamera();
    Fenrir::Overlay& overlay = OverlayOf(test);

    overlay.Submit(MainWindow, 0, Painted(painting, Fenrir::BlendSpace2D::Linear));
    test.Frame();
    const std::size_t pipelines = RendererProbe::LivePipelines(test.Device());
    const std::vector<RendererTest::RecordedDraw> first = test.Draws("Overlay 2D");

    INFO("the list's two shapes are one draw of builtin:painter's pipeline");
    REQUIRE(first.size() == 1);
    REQUIRE(first[0].draw.indexCount == 12);
    REQUIRE(RendererProbe::Label(test.Device(), first[0].pipeline) == "BuiltinPainter");
    const RendererProbe::PipelineState state = RendererProbe::State(test.Device(), first[0].pipeline);
    INFO("premultiplied alpha, encoding linear values into the sRGB backbuffer");
    REQUIRE(state.blend == Fenrir::Graphics::BlendMode::Premultiplied);
    REQUIRE(state.encodeSrgb);

    overlay.Submit(MainWindow, 0, Painted(painting, Fenrir::BlendSpace2D::Linear));
    overlay.Submit(MainWindow, 1, Painted(painting, Fenrir::BlendSpace2D::Linear));
    test.Frame();
    const std::vector<RendererTest::RecordedDraw> later = test.Draws("Overlay 2D");

    INFO("two lists the next frame, with equal descriptions, draw with the same pipeline and make no other");
    REQUIRE(later.size() == 2);
    REQUIRE(later[0].pipeline == first[0].pipeline);
    REQUIRE(later[1].pipeline == first[0].pipeline);
    REQUIRE(RendererProbe::LivePipelines(test.Device()) == pipelines);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("builtin:painter's variants compile for both blend spaces", "[painter][pipelines]")
{
    RendererTestApp test;
    const Fenrir::Painting painting = Fenrir::Painting::Install(test.app);
    test.AddCamera();
    Fenrir::Overlay& overlay = OverlayOf(test);

    overlay.Submit(MainWindow, 0, Painted(painting, Fenrir::BlendSpace2D::Linear));
    overlay.Submit(MainWindow, 1, Painted(painting, Fenrir::BlendSpace2D::Gamma));
    test.Frame();
    const std::vector<RendererTest::RecordedDraw> draws = test.Draws("Overlay 2D");

    REQUIRE(draws.size() == 2);
    INFO("the Linear root's draw uses the base variant, encoding into the sRGB backbuffer");
    REQUIRE(RendererProbe::Keys(test.Device(), draws[0].pipeline).empty());
    REQUIRE(RendererProbe::State(test.Device(), draws[0].pipeline).encodeSrgb);
    INFO("the Gamma root's draw uses the GAMMA variant, with encoding off");
    REQUIRE(RendererProbe::Keys(test.Device(), draws[1].pipeline) == std::vector<std::string>{"GAMMA"});
    REQUIRE_FALSE(RendererProbe::State(test.Device(), draws[1].pipeline).encodeSrgb);
    INFO("two pipelines, and nothing logged");
    REQUIRE(draws[0].pipeline != draws[1].pipeline);
    REQUIRE(test.messages->errors.empty());
}
