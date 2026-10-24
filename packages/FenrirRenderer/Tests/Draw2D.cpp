// The renderer core's 2D render feature alone (ADR-0027), with a fake surface, the null backend and no App (the spec's
// seam 1): how a 2D draw list's scissors resolve to pixels, and which commands are skipped.

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/BuiltinShaders.hpp"
#include "Core/Draw2DFeature.hpp"
#include "Core/RendererCore.hpp"
#include "Core/SceneRenderer.hpp"
#include "DrawLists2D.hpp"
#include "FakeSurface.hpp"
#include "RecordingLogger.hpp"
#include "RendererProbe.hpp"
#include "RendererTestApp.hpp"

using Fenrir::RendererProbe;
using RendererTest::AddQuad;
using RendererTest::Pipeline2D;

namespace
{
    /** @brief The asset key builtin:2d is prepared under. */
    constexpr Fenrir::Render::AssetKey Builtin2DKey{1, 1};

    /** @brief A renderer core with the scene renderer and the 2D feature, on a 64 x 32 fake surface. */
    struct Core2D
    {
        RendererTest::FakeSurface surface;
        std::shared_ptr<RendererTest::LoggedMessages> messages = std::make_shared<RendererTest::LoggedMessages>();
        RendererTest::RecordingLogger logger{messages};
        Fenrir::Render::RendererCore core{Fenrir::Graphics::CreateNullDevice(), surface};
        Fenrir::Render::Draw2DFeature* draw2D = nullptr;

        Core2D()
        {
            auto scene = std::make_unique<Fenrir::Render::SceneRenderer>(core.Device(), logger);
            auto feature = std::make_unique<Fenrir::Render::Draw2DFeature>(core.Device(), logger, *scene);
            draw2D = feature.get();
            core.AddFeature(std::move(scene));
            core.AddFeature(std::move(feature));
            Fenrir::ShaderData shader;
            const auto bytes = Fenrir::Render::GetBuiltinShader(Fenrir::Render::BuiltinProgram::Draw2D).bytes;
            shader.bytes.assign(bytes.begin(), bytes.end());
            draw2D->PrepareShader(Builtin2DKey, 1, shader);
        }

        /** @brief A list with one builtin:2d pipeline and no commands. */
        static Fenrir::DrawList2D List()
        {
            Fenrir::DrawList2D list;
            list.AddPipeline(Pipeline2D({Builtin2DKey.index, Builtin2DKey.generation}));
            return list;
        }

        /** @brief Draws `list` on layer 0 at `contentScale` in one frame. */
        void Draw(Fenrir::DrawList2D list, float contentScale = 1.0f)
        {
            Fenrir::Render::OverlayFrame frame;
            frame.submissions.push_back({0, std::move(list)});
            frame.contentScale = contentScale;
            draw2D->SetFrame(std::move(frame));
            core.RenderFrame();
        }

        /** @brief The scissor each indexed draw of the last frame drew with, in order. */
        [[nodiscard]] std::vector<Fenrir::Graphics::PixelRect> Scissors() const
        {
            using namespace Fenrir::Graphics;
            std::vector<PixelRect> scissors;
            PixelRect scissor;
            for (const CommandList& list : RendererProbe::Submitted(core))
            {
                for (const Command& command : list.commands)
                {
                    if (const auto* set = std::get_if<CmdSetScissor>(&command))
                    {
                        scissor = set->rect;
                    }
                    else if (std::holds_alternative<CmdDrawIndexed>(command))
                    {
                        scissors.push_back(scissor);
                    }
                }
            }
            return scissors;
        }
    };

    /** @brief A list of one full-surface quad drawn once per scissor (none for nullopt), in order. */
    Fenrir::DrawList2D Scissored(const std::vector<std::optional<Fenrir::Rect2D>>& scissors)
    {
        Fenrir::DrawList2D list = Core2D::List();
        for (const std::optional<Fenrir::Rect2D>& scissor : scissors)
        {
            AddQuad(list, 0, {0.0f, 0.0f}, {64.0f, 32.0f}).scissor = scissor;
        }
        return list;
    }
} // namespace

TEST_CASE("Scissors resolve to whole pixels rounded outwards and clamped, at content scale 1", "[renderer][overlay]")
{
    Core2D test;

    test.Draw(Scissored(
        {Fenrir::Rect2D{10.2f, 4.5f, 20.1f, 10.0f}, Fenrir::Rect2D{50.0f, 20.0f, 100.0f, 100.0f}, std::nullopt}));
    const std::vector<Fenrir::Graphics::PixelRect> scissors = test.Scissors();

    INFO("10.2 to 30.3 across rounds out to pixels 10 to 31, 4.5 to 14.5 down to 4 to 15");
    REQUIRE(scissors.size() == 3);
    REQUIRE(scissors[0] == Fenrir::Graphics::PixelRect{10, 4, 21, 11});
    INFO("a scissor past the 64 x 32 surface is clamped to it");
    REQUIRE(scissors[1] == Fenrir::Graphics::PixelRect{50, 20, 14, 12});
    INFO("no scissor is the whole surface");
    REQUIRE(scissors[2] == Fenrir::Graphics::PixelRect{0, 0, 64, 32});
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Scissors are scaled by a content scale of 1.5 before they are rounded outwards", "[renderer][overlay]")
{
    Core2D test;

    test.Draw(Scissored({Fenrir::Rect2D{10.2f, 4.5f, 20.1f, 10.0f}, Fenrir::Rect2D{-5.0f, -5.0f, 10.0f, 10.0f}}), 1.5f);
    const std::vector<Fenrir::Graphics::PixelRect> scissors = test.Scissors();

    INFO("15.3 to 45.45 across rounds out to pixels 15 to 46, 6.75 to 21.75 down to 6 to 22");
    REQUIRE(scissors.size() == 2);
    REQUIRE(scissors[0] == Fenrir::Graphics::PixelRect{15, 6, 31, 16});
    INFO("-7.5 to 7.5 each way rounds out to -8 to 8, clamped to the surface's corner at 0");
    REQUIRE(scissors[1] == Fenrir::Graphics::PixelRect{0, 0, 8, 8});
}

TEST_CASE("A command over a limit is skipped and each limit logged once, the rest of the list drawn",
          "[renderer][overlay]")
{
    Core2D test;
    const auto list = [] {
        Fenrir::DrawList2D made = Core2D::List();
        AddQuad(made, 0, {0.0f, 0.0f}, {8.0f, 8.0f}).textures.resize(Fenrir::MaxTextureSlots2D + 1);
        AddQuad(made, 0, {0.0f, 0.0f}, {8.0f, 8.0f}).parameters.resize(Fenrir::MaxParameterBytes2D + 1);
        AddQuad(made, 0, {0.0f, 0.0f}, {8.0f, 8.0f}).pipeline = 3;
        AddQuad(made, 0, {0.0f, 0.0f}, {8.0f, 8.0f}).indexCount = 600;
        AddQuad(made, 0, {0.0f, 0.0f}, {8.0f, 8.0f}).textures.resize(Fenrir::MaxTextureSlots2D);
        return made;
    };

    test.Draw(list());
    INFO("only the last command, at the limits, is drawn");
    REQUIRE(test.Scissors().size() == 1);
    test.Draw(list());
    REQUIRE(test.Scissors().size() == 1);

    INFO("each skipped command's reason was logged once over both frames");
    REQUIRE(test.messages->errors.size() == 4);
    REQUIRE(RendererTest::CountContaining(test.messages->errors, "5 texture slots, more than 4") == 1);
    REQUIRE(RendererTest::CountContaining(test.messages->errors, "129 parameter bytes, more than 128") == 1);
    REQUIRE(RendererTest::CountContaining(test.messages->errors, "names pipeline 3 of a table of 1") == 1);
    REQUIRE(RendererTest::CountContaining(test.messages->errors, "past its 30 indices") == 1);
}

TEST_CASE("Nothing but pipelines is kept between frames: a frame with no submissions adds no pass",
          "[renderer][overlay]")
{
    Core2D test;
    test.Draw(Scissored({std::nullopt}));
    const std::size_t pipelines = RendererProbe::LivePipelines(test.core.Device());

    test.core.RenderFrame();

    INFO("the next frame, given nothing, draws no 2D pass and makes no pipeline");
    REQUIRE(test.Scissors().empty());
    REQUIRE(RendererProbe::LivePipelines(test.core.Device()) == pipelines);
}
