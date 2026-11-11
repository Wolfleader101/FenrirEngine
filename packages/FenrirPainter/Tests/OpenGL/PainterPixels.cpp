// builtin:painter on OpenGL (the spec's GL check): a rounded rectangle, a radial gradient and a bordered box painted
// into a small offscreen image by the renderer core's 2D render feature, as the renderer's own overlay GL tests draw,
// and read back at their centres and edges. The target is linear RGBA16F, cleared transparent, and the root's blend
// space Linear, so a pixel is the shader's premultiplied linear output; the expected values are worked by hand.

#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/Draw2DFeature.hpp"
#include "Core/RendererCore.hpp"
#include "Core/SceneRenderer.hpp"
#include "FenrirPainter/Painter.hpp"
#include "FenrirShaders/BuiltinPainter.hpp"
#include "GlProbe.hpp"
#include "GlTestApp.hpp"
#include "RecordingLogger.hpp"

using Fenrir::Colour;
using Fenrir::GlProbe;
using Fenrir::Graphics::TextureFormat;

namespace
{
    constexpr std::uint32_t Size = GlTest::TargetSize;
    /** @brief The asset key builtin:painter is prepared under. */
    constexpr Fenrir::Render::AssetKey PainterKey{1, 1};
    /** @brief Beside the hand-worked colours, the error function's and the 16-bit float's error. */
    constexpr float Tolerance = 0.01f;

    /** @brief An sRGB-encoded channel decoded to linear. */
    float Decode(float srgb)
    {
        return srgb <= 0.04045f ? srgb / 12.92f : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
    }

    /**
     * @brief The renderer core on the GL tests' device, with the 2D render feature and builtin:painter prepared,
     * drawing a list into a Size-square RGBA16F image cleared to transparent black.
     */
    class GlPainter
    {
      public:
        GlPainter()
        {
            gl.RequireOpenGL();
            core = std::make_unique<Fenrir::Render::RendererCore>(std::move(gl.device), gl.surface);
            auto scene = std::make_unique<Fenrir::Render::SceneRenderer>(core->Device(), logger);
            auto feature = std::make_unique<Fenrir::Render::Draw2DFeature>(core->Device(), logger, *scene);
            draw2D = feature.get();
            core->AddFeature(std::move(scene));
            core->AddFeature(std::move(feature));
            Fenrir::ShaderData shader;
            const auto bytes = std::as_bytes(std::span(Fenrir::Shaders::BuiltinPainter));
            shader.bytes.assign(bytes.begin(), bytes.end());
            draw2D->PrepareShader(PainterKey, 1, shader);
        }

        /** @brief A painter config on builtin:painter, Linear. */
        static Fenrir::PainterConfig Config()
        {
            Fenrir::PainterConfig config;
            config.shader = {PainterKey.index, PainterKey.generation};
            return config;
        }

        /** @brief Draws `list` into a fresh target and reads it back. */
        GlProbe::Image Draw(Fenrir::DrawList2D list)
        {
            using namespace Fenrir::Graphics;
            TextureDesc desc;
            desc.format = TextureFormat::Rgba16Float;
            desc.width = Size;
            desc.height = Size;
            desc.usage = TextureUsage::ColourAttachment | TextureUsage::Sampled;
            const TextureHandle target = core->Device().CreateTexture(desc, "Painter test target");
            CommandList clear;
            {
                PassDesc pass;
                pass.label = "Clear";
                pass.colour.push_back({target, LoadOp::Clear, {0.0f, 0.0f, 0.0f, 0.0f}});
                pass.uses.push_back({target, {}, ResourceUsage::ColourAttachment});
                PassEncoder encoder(clear, pass);
            }
            core->Device().Submit(clear);

            Fenrir::Render::OverlayFrame frame;
            frame.submissions.push_back({0, std::move(list)});
            frame.target = target;
            frame.targetDesc = desc;
            draw2D->SetFrame(std::move(frame));
            const Fenrir::Render::FrameReport report = core->RenderFrame();
            INFO("the frame was drawn");
            REQUIRE(report.drawn);
            return GlProbe::Read(core->Device(), target);
        }

        GlTest::GlTestApp gl;
        std::shared_ptr<RendererTest::LoggedMessages> messages = std::make_shared<RendererTest::LoggedMessages>();
        RendererTest::RecordingLogger logger{messages};
        std::unique_ptr<Fenrir::Render::RendererCore> core;
        Fenrir::Render::Draw2DFeature* draw2D = nullptr;
    };

    constexpr GlProbe::Pixel Transparent{0.0f, 0.0f, 0.0f, 0.0f};
} // namespace

TEST_CASE("A rounded rectangle is solid inside, half covered on an edge through pixel centres, and cut at its corners",
          "[painter][gl]")
{
    GlPainter test;
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, GlPainter::Config());
    // Its left edge runs through the centres of column 8, so they are half covered.
    painter.DrawRoundedRect({8.5f, 8.5f, 47.0f, 47.0f}, Fenrir::CornerRadii::All(12), Colour::Red);

    const GlProbe::Image image = test.Draw(std::move(list));

    INFO("opaque red at the centre and just inside the top edge");
    REQUIRE(GlProbe::Near(image.At(32, 32), {1.0f, 0.0f, 0.0f, 1.0f}, Tolerance));
    REQUIRE(GlProbe::Near(image.At(32, 9), {1.0f, 0.0f, 0.0f, 1.0f}, Tolerance));
    INFO("half red, premultiplied, on the left edge");
    REQUIRE(GlProbe::Near(image.At(8, 32), {0.5f, 0.0f, 0.0f, 0.5f}, Tolerance));
    INFO("nothing outside it, nor in the rounded-off corner");
    REQUIRE(GlProbe::Near(image.At(6, 32), Transparent, Tolerance));
    REQUIRE(GlProbe::Near(image.At(10, 10), Transparent, Tolerance));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A radial gradient blends from its centre out to its radius", "[painter][gl]")
{
    GlPainter test;
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, GlPainter::Config());
    Fenrir::RadialGradient gradient;
    gradient.centre = {32, 32};
    gradient.radius = 32;
    gradient.focal = {32, 32};
    gradient.stops = {{0.0f, Colour::White}, {1.0f, Colour::Black}};
    painter.DrawRect({0, 0, 64, 64}, gradient);

    const GlProbe::Image image = test.Draw(std::move(list));

    // The stops blend in sRGB: at offset t a pixel is sRGB 1 - t, decoded. Pixel (x, 32)'s centre is (x + 0.5, 32.5).
    const auto expected = [](float x) {
        const float t = std::sqrt((x + 0.5f - 32.0f) * (x + 0.5f - 32.0f) + 0.25f) / 32.0f;
        const float value = Decode(1.0f - std::min(t, 1.0f));
        return GlProbe::Pixel{value, value, value, 1.0f};
    };
    INFO("near white at the centre, sRGB 0.48 halfway out, near black at the edge");
    REQUIRE(GlProbe::Near(image.At(32, 32), expected(32), Tolerance));
    REQUIRE(GlProbe::Near(image.At(48, 32), expected(48), Tolerance));
    REQUIRE(GlProbe::Near(image.At(1, 32), expected(1), Tolerance));
    INFO("and black past the radius, in the corner, padded");
    REQUIRE(GlProbe::Near(image.At(1, 1), {0.0f, 0.0f, 0.0f, 1.0f}, Tolerance));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A bordered box shows each side's colour over its fill, and nothing outside", "[painter][gl]")
{
    GlPainter test;
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, GlPainter::Config());
    Fenrir::Border border = Fenrir::Border::Uniform(4, Colour::White);
    border.top = Colour::Red;
    border.left = Colour::Green;
    painter.DrawRect({16, 16, 32, 32}, Colour::Blue);
    painter.DrawBorder({16, 16, 32, 32}, {}, border);

    const GlProbe::Image image = test.Draw(std::move(list));

    INFO("the fill's blue in the middle");
    REQUIRE(GlProbe::Near(image.At(32, 32), {0.0f, 0.0f, 1.0f, 1.0f}, Tolerance));
    INFO("the top side red, the left green, the right and bottom white, each 4 pixels wide");
    REQUIRE(GlProbe::Near(image.At(32, 17), {1.0f, 0.0f, 0.0f, 1.0f}, Tolerance));
    REQUIRE(GlProbe::Near(image.At(17, 32), {0.0f, 1.0f, 0.0f, 1.0f}, Tolerance));
    REQUIRE(GlProbe::Near(image.At(46, 32), {1.0f, 1.0f, 1.0f, 1.0f}, Tolerance));
    REQUIRE(GlProbe::Near(image.At(32, 46), {1.0f, 1.0f, 1.0f, 1.0f}, Tolerance));
    INFO("the fill again just inside the border, and nothing just outside the box");
    REQUIRE(GlProbe::Near(image.At(20, 32), {0.0f, 0.0f, 1.0f, 1.0f}, Tolerance));
    REQUIRE(GlProbe::Near(image.At(15, 32), Transparent, Tolerance));
    REQUIRE(test.messages->errors.empty());
}
