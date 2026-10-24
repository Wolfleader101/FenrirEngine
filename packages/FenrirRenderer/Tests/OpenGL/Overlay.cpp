// The 2D render feature on OpenGL (ADR-0027, the spec's seam 2): 2D draw lists drawn into small offscreen targets by
// the renderer core, as the window overlay draws them into the backbuffer, and read back. The expected colours are
// ADR-0027's blend table worked by hand.

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/BuiltinShaders.hpp"
#include "Core/Draw2DFeature.hpp"
#include "Core/RendererCore.hpp"
#include "Core/SceneRenderer.hpp"
#include "DrawLists2D.hpp"
#include "GlProbe.hpp"
#include "GlTestApp.hpp"
#include "RecordingLogger.hpp"
#include "RendererTestApp.hpp"

using Fenrir::GlProbe;
using Fenrir::Graphics::TextureFormat;
using RendererTest::AddQuad;
using RendererTest::Rgba8;

namespace
{
    constexpr std::uint32_t Size = GlTest::TargetSize;
    /** @brief The asset key builtin:2d is prepared under. */
    constexpr Fenrir::Render::AssetKey Builtin2DKey{1, 1};
    /** @brief 128 of 255. */
    constexpr float Half = 128.0f / 255.0f;
    /** @brief 128 of 255 decoded from sRGB. */
    constexpr float HalfDecoded = 0.2158605f;

    /**
     * @brief The renderer core on the GL tests' device, with the scene renderer and the 2D feature, drawing 2D draw
     * lists into an offscreen target of its own. The features log to their own recording logger, so a test can expect
     * an error from them; the device's errors still fail the test.
     */
    class GlCore2D
    {
      public:
        GlCore2D()
        {
            gl.RequireOpenGL();
            core = std::make_unique<Fenrir::Render::RendererCore>(std::move(gl.device), gl.surface);
            auto made = std::make_unique<Fenrir::Render::SceneRenderer>(core->Device(), logger);
            auto feature = std::make_unique<Fenrir::Render::Draw2DFeature>(core->Device(), logger, *made);
            scene = made.get();
            draw2D = feature.get();
            core->AddFeature(std::move(made));
            core->AddFeature(std::move(feature));
            Fenrir::ShaderData shader;
            const auto bytes = Fenrir::Render::GetBuiltinShader(Fenrir::Render::BuiltinProgram::Draw2D).bytes;
            shader.bytes.assign(bytes.begin(), bytes.end());
            draw2D->PrepareShader(Builtin2DKey, 1, shader);
        }

        /** @brief A Size-square target of `format`, cleared to `colour`. */
        void MakeTarget(TextureFormat format, GlProbe::Pixel colour)
        {
            using namespace Fenrir::Graphics;
            m_desc.format = format;
            m_desc.width = Size;
            m_desc.height = Size;
            m_desc.usage = TextureUsage::ColourAttachment | TextureUsage::Sampled;
            m_target = core->Device().CreateTexture(m_desc, "2D test target");
            CommandList list;
            {
                PassDesc pass;
                pass.label = "Clear";
                pass.colour.push_back({m_target, LoadOp::Clear, colour});
                pass.uses.push_back({m_target, {}, ResourceUsage::ColourAttachment});
                PassEncoder encoder(list, pass);
            }
            core->Device().Submit(list);
        }

        /** @brief A list with one builtin:2d pipeline described by `pipeline` over its shader. */
        static Fenrir::DrawList2D List(Fenrir::PipelineDescription2D pipeline)
        {
            pipeline.shader = {Builtin2DKey.index, Builtin2DKey.generation};
            Fenrir::DrawList2D list;
            list.AddPipeline(pipeline);
            return list;
        }

        /** @brief Draws `submissions` (layer and list, in submission order) into the target and reads it back. */
        GlProbe::Image Draw(std::vector<Fenrir::Render::Submission2D> submissions, float contentScale = 1.0f)
        {
            Fenrir::Render::OverlayFrame frame;
            frame.submissions = std::move(submissions);
            frame.contentScale = contentScale;
            frame.target = m_target;
            frame.targetDesc = m_desc;
            draw2D->SetFrame(std::move(frame));
            const Fenrir::Render::FrameReport report = core->RenderFrame();
            INFO("the frame was drawn");
            REQUIRE(report.drawn);
            return GlProbe::Read(core->Device(), m_target);
        }

        GlTest::GlTestApp gl;
        std::shared_ptr<RendererTest::LoggedMessages> messages = std::make_shared<RendererTest::LoggedMessages>();
        RendererTest::RecordingLogger logger{messages};
        std::unique_ptr<Fenrir::Render::RendererCore> core;
        Fenrir::Render::SceneRenderer* scene = nullptr;
        Fenrir::Render::Draw2DFeature* draw2D = nullptr;

      private:
        Fenrir::Graphics::TextureHandle m_target;
        Fenrir::Graphics::TextureDesc m_desc;
    };

    /** @brief A pipeline description of `blend`, with linear vertex colours. */
    Fenrir::PipelineDescription2D Linear(Fenrir::BlendMode2D blend)
    {
        Fenrir::PipelineDescription2D pipeline;
        pipeline.vertexLayout = Fenrir::Vertex2DLayout();
        pipeline.blendMode = blend;
        pipeline.inputColourSpace = Fenrir::ColourSpace::Linear;
        return pipeline;
    }

    /** @brief One list covering the whole target with a quad of `colour`, drawn with `pipeline`. */
    std::vector<Fenrir::Render::Submission2D> FullQuad(const Fenrir::PipelineDescription2D& pipeline,
                                                       std::uint32_t colour)
    {
        Fenrir::DrawList2D list = GlCore2D::List(pipeline);
        AddQuad(list, 0, {0.0f, 0.0f}, {static_cast<float>(Size), static_cast<float>(Size)}, colour);
        std::vector<Fenrir::Render::Submission2D> submissions;
        submissions.push_back({0, std::move(list)});
        return submissions;
    }

    constexpr GlProbe::Pixel OpaqueBlue{0.0f, 0.0f, 1.0f, 1.0f};
    constexpr GlProbe::Pixel HalfBlue{0.0f, 0.0f, 1.0f, 0.5f};
    /** @brief Linear red at alpha 128/255. */
    constexpr std::uint32_t HalfRed = Rgba8(255, 0, 0, 128);
} // namespace

TEST_CASE("A straight-alpha quad over an opaque and a half-transparent target blends as ADR-0027's table gives",
          "[renderer][gl][overlay]")
{
    GlCore2D test;
    // RGB: src * a + dst * (1 - a). Alpha: a + dstA * (1 - a).
    test.MakeTarget(TextureFormat::Rgba16Float, OpaqueBlue);
    const GlProbe::Pixel overOpaque = test.Draw(FullQuad(Linear(Fenrir::BlendMode2D::AlphaBlend), HalfRed)).At(32, 32);
    test.MakeTarget(TextureFormat::Rgba16Float, HalfBlue);
    const GlProbe::Pixel overHalf = test.Draw(FullQuad(Linear(Fenrir::BlendMode2D::AlphaBlend), HalfRed)).At(32, 32);

    INFO("over opaque blue: half red, half blue, alpha 1");
    REQUIRE(GlProbe::Near(overOpaque, {Half, 0.0f, 1.0f - Half, 1.0f}));
    INFO("over blue at alpha 0.5: the same colour, alpha a + 0.5 (1 - a)");
    REQUIRE(GlProbe::Near(overHalf, {Half, 0.0f, 1.0f - Half, Half + 0.5f * (1.0f - Half)}));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A premultiplied quad, premultiplied by builtin:2d, gives the straight quad's result",
          "[renderer][gl][overlay]")
{
    GlCore2D test;
    // builtin:2d multiplies RGB by alpha; then RGB: src + dst * (1 - a). Alpha: a + dstA * (1 - a).
    test.MakeTarget(TextureFormat::Rgba16Float, OpaqueBlue);
    const GlProbe::Pixel overOpaque =
        test.Draw(FullQuad(Linear(Fenrir::BlendMode2D::PremultipliedAlpha), HalfRed)).At(32, 32);
    test.MakeTarget(TextureFormat::Rgba16Float, HalfBlue);
    const GlProbe::Pixel overHalf =
        test.Draw(FullQuad(Linear(Fenrir::BlendMode2D::PremultipliedAlpha), HalfRed)).At(32, 32);

    INFO("over opaque blue: half red, half blue, alpha 1");
    REQUIRE(GlProbe::Near(overOpaque, {Half, 0.0f, 1.0f - Half, 1.0f}));
    INFO("over blue at alpha 0.5: alpha a + 0.5 (1 - a)");
    REQUIRE(GlProbe::Near(overHalf, {Half, 0.0f, 1.0f - Half, Half + 0.5f * (1.0f - Half)}));
}

TEST_CASE("An additive quad adds its colour and keeps the target's alpha; an opaque one writes its own",
          "[renderer][gl][overlay]")
{
    GlCore2D test;
    test.MakeTarget(TextureFormat::Rgba16Float, HalfBlue);
    const GlProbe::Pixel added = test.Draw(FullQuad(Linear(Fenrir::BlendMode2D::Additive), HalfRed)).At(32, 32);
    test.MakeTarget(TextureFormat::Rgba16Float, HalfBlue);
    const GlProbe::Pixel opaque = test.Draw(FullQuad(Linear(Fenrir::BlendMode2D::Opaque), HalfRed)).At(32, 32);

    INFO("additive, One / One with alpha Zero / One: red plus blue, the target's alpha 0.5 kept");
    REQUIRE(GlProbe::Near(added, {1.0f, 0.0f, 1.0f, 0.5f}));
    INFO("opaque: the quad's colour and alpha written as they are");
    REQUIRE(GlProbe::Near(opaque, {1.0f, 0.0f, 0.0f, Half}));
}

TEST_CASE("sRGB vertex colours are decoded, and Gamma on a target that is not 8-bit sRGB logs and draws as Linear",
          "[renderer][gl][overlay]")
{
    GlCore2D test;
    Fenrir::PipelineDescription2D srgb = Linear(Fenrir::BlendMode2D::Opaque);
    srgb.inputColourSpace = Fenrir::ColourSpace::Srgb;
    Fenrir::PipelineDescription2D gamma = srgb;
    gamma.blendSpace = Fenrir::BlendSpace2D::Gamma;
    const std::uint32_t grey = Rgba8(128, 128, 128, 255);

    test.MakeTarget(TextureFormat::Rgba16Float, OpaqueBlue);
    const GlProbe::Pixel linear = test.Draw(FullQuad(srgb, grey)).At(32, 32);
    REQUIRE(test.messages->errors.empty());
    test.MakeTarget(TextureFormat::Rgba16Float, OpaqueBlue);
    const GlProbe::Pixel asLinear = test.Draw(FullQuad(gamma, grey)).At(32, 32);

    INFO("sRGB 128 decodes to linear 0.216 in a linear blend");
    REQUIRE(GlProbe::Near(linear, {HalfDecoded, HalfDecoded, HalfDecoded, 1.0f}));
    INFO("the Gamma pipeline on RGBA16F logged one error, naming the target's format");
    REQUIRE(RendererTest::CountContaining(test.messages->errors, "Gamma") == 1);
    REQUIRE(RendererTest::CountContaining(test.messages->errors, "RGBA16F") == 1);
    INFO("and drew as Linear: decoded, not passed through as 0.502");
    REQUIRE(GlProbe::Near(asLinear, {HalfDecoded, HalfDecoded, HalfDecoded, 1.0f}));
}

TEST_CASE("A lower overlay layer draws under a higher one submitted before it", "[renderer][gl][overlay]")
{
    GlCore2D test;
    test.MakeTarget(TextureFormat::Rgba16Float, {0.0f, 0.0f, 0.0f, 1.0f});
    const Fenrir::PipelineDescription2D opaque = Linear(Fenrir::BlendMode2D::Opaque);
    Fenrir::DrawList2D red = GlCore2D::List(opaque);
    AddQuad(red, 0, {0.0f, 0.0f}, {32.0f, 64.0f}, Rgba8(255, 0, 0, 255));
    Fenrir::DrawList2D green = GlCore2D::List(opaque);
    AddQuad(green, 0, {0.0f, 0.0f}, {64.0f, 64.0f}, Rgba8(0, 255, 0, 255));
    std::vector<Fenrir::Render::Submission2D> submissions;
    submissions.push_back({1, std::move(red)});
    submissions.push_back({0, std::move(green)});

    const GlProbe::Image image = test.Draw(std::move(submissions));

    INFO("layer 1's red half covers layer 0's green, though it was submitted first");
    REQUIRE(GlProbe::Near(image.At(16, 32), {1.0f, 0.0f, 0.0f, 1.0f}));
    INFO("layer 0's green shows where layer 1 drew nothing");
    REQUIRE(GlProbe::Near(image.At(48, 32), {0.0f, 1.0f, 0.0f, 1.0f}));
}

TEST_CASE("A scissor at content scale 1.5 clips to its rectangle scaled and rounded outwards",
          "[renderer][gl][overlay]")
{
    GlCore2D test;
    test.MakeTarget(TextureFormat::Rgba16Float, {0.0f, 0.0f, 0.0f, 1.0f});
    Fenrir::DrawList2D list = GlCore2D::List(Linear(Fenrir::BlendMode2D::Opaque));
    // The whole 64-pixel target is 42.67 logical units at 1.5; the scissor 10.2 to 20.2 is pixels 15.3 to 30.3,
    // rounded out to 15 to 31.
    AddQuad(list, 0, {0.0f, 0.0f}, {43.0f, 43.0f}, Rgba8(255, 255, 255, 255)).scissor =
        Fenrir::Rect2D{10.2f, 10.2f, 10.0f, 10.0f};
    std::vector<Fenrir::Render::Submission2D> submissions;
    submissions.push_back({0, std::move(list)});

    const GlProbe::Image image = test.Draw(std::move(submissions), 1.5f);
    const GlProbe::Pixel white{1.0f, 1.0f, 1.0f, 1.0f};
    const GlProbe::Pixel black{0.0f, 0.0f, 0.0f, 1.0f};

    INFO("pixels 15 to 30 across and down are drawn, from the top-left");
    REQUIRE(GlProbe::Near(image.At(15, 15), white));
    REQUIRE(GlProbe::Near(image.At(30, 30), white));
    REQUIRE(GlProbe::Near(image.At(15, 30), white));
    INFO("the pixels just outside are not");
    REQUIRE(GlProbe::Near(image.At(14, 20), black));
    REQUIRE(GlProbe::Near(image.At(20, 14), black));
    REQUIRE(GlProbe::Near(image.At(31, 20), black));
    REQUIRE(GlProbe::Near(image.At(20, 31), black));
}

TEST_CASE("A texture slot's sRGB image is sampled decoded, times the vertex colour", "[renderer][gl][overlay]")
{
    GlCore2D test;
    test.MakeTarget(TextureFormat::Rgba16Float, {0.0f, 0.0f, 0.0f, 1.0f});
    const Fenrir::Render::AssetKey image{5, 1};
    test.scene->PrepareImage(image, 1, RendererTest::Picture(1, 1, {255, 128, 0, 255}));
    Fenrir::DrawList2D list = GlCore2D::List(Linear(Fenrir::BlendMode2D::Opaque));
    AddQuad(list, 0, {0.0f, 0.0f}, {64.0f, 64.0f}, Rgba8(255, 255, 255, 255))
        .textures.push_back({Fenrir::AssetHandle<Fenrir::Image>{image.index, image.generation}});
    std::vector<Fenrir::Render::Submission2D> submissions;
    submissions.push_back({0, std::move(list)});

    const GlProbe::Pixel pixel = test.Draw(std::move(submissions)).At(32, 32);

    INFO("the image's orange, 128 decoded to 0.216");
    REQUIRE(GlProbe::Near(pixel, {1.0f, HalfDecoded, 0.0f, 1.0f}));
}
