// Occlusion culling on OpenGL (ADR-0057, culling ticket 11), read back from the camera's command buffer: a square
// wholly behind a wall is drawn in neither phase once the wall has drawn in the first, and one that moves out from
// behind it is drawn in the second phase the same frame; drawnByPhase reads those counts back; frames with occlusion
// culling on and off are the same pixel for pixel as the camera moves through a scene of walls and copies; and the
// pyramid's levels hold the farthest depth of a step as well as the nearest.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Device/CommandList.hpp"
#include "FenrirAssets/InstanceListData.hpp"
#include "GlScene.hpp"

using Fenrir::GlProbe;
using Fenrir::RendererProbe;
using Fenrir::Math::Vec3;
using namespace Fenrir::Graphics;

namespace
{
    constexpr std::size_t CommandSize = sizeof(DrawIndexedIndirectCommand);

    /**
     * @brief GlScene with a camera entity at the origin looking along -Z, its bloom off, a white unlit double-sided
     * material and a square of side 0.4, culling by occlusion as the renderer does by default.
     */
    class GlOcclusionScene : public GlTest::GlScene
    {
      public:
        GlOcclusionScene()
        {
            camera = app.GetActiveScene().GetEntityList().CreateEntity();
            camera.AddComponent<Fenrir::Camera>().bloom.intensity = 0.0f;
            Fenrir::AssetStore& store = assets.Store();
            white = store.CreateMaterial(store.Load<Fenrir::Shader>("builtin:unlit", Fenrir::AssetOwner::App()).handle,
                                         Fenrir::AssetOwner::App());
            store.SetColour(white, "baseColour", Fenrir::Colour::White);
            store.SetDoubleSided(white, true);
            square = store.Add<Fenrir::Mesh>(RendererTest::Quad(0.4f, 0.0f), Fenrir::AssetOwner::App());
        }

        Fenrir::Entity camera;
        Fenrir::AssetHandle<Fenrir::Material> white;
        Fenrir::AssetHandle<Fenrir::Mesh> square;

        /** @brief A MeshRenderer of the square at `position`, scaled by `scale`, in the white material. */
        Fenrir::Entity AddSquare(Vec3 position, float scale = 1.0f)
        {
            Fenrir::Entity entity = app.GetActiveScene().GetEntityList().CreateEntity();
            entity.GetComponent<Fenrir::Transform>().pos = position;
            entity.GetComponent<Fenrir::Transform>().scale = Vec3(scale);
            entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{square, white, true});
            return entity;
        }

        /** @brief The instance count of command `command` of the camera's command buffer, as the frame left it. */
        std::uint32_t Count(std::size_t command)
        {
            const std::vector<RendererProbe::CullBuffers> culls = RendererProbe::Culls(renderer);
            REQUIRE(culls.size() == 1);
            const std::vector<std::byte> commands =
                GlProbe::ReadBuffer(RendererProbe::Device(renderer), culls.front().commands);
            return RendererTest::CommandWord(commands, command * CommandSize, 1);
        }
    };

    /** @brief Whether every pixel of `a` is within GlProbe's tolerance of `b`'s. */
    void RequireSame(const GlProbe::Image& a, const GlProbe::Image& b)
    {
        REQUIRE(a.width == b.width);
        REQUIRE(a.height == b.height);
        for (std::uint32_t y = 0; y < a.height; ++y)
        {
            for (std::uint32_t x = 0; x < a.width; ++x)
            {
                CAPTURE(x, y);
                REQUIRE(GlProbe::Near(a.At(x, y), b.At(x, y)));
            }
        }
    }

    bool Close(float a, float b, float tolerance)
    {
        return std::abs(a - b) <= tolerance;
    }
} // namespace

TEST_CASE("GL: a square wholly behind a wall is not drawn in the second phase once the wall has drawn in the first, "
          "and one that comes out from behind it is drawn the same frame",
          "[renderer][gl][culling][occlusion][readback]")
{
    GlOcclusionScene test;
    test.RequireOpenGL();
    // A wall two units across at three units, and the square at four behind its centre: one merged run of two
    // candidates, whose first-phase command is 0 and second-phase command 1.
    test.AddSquare({0.0f, 0.0f, -3.0f}, 5.0f);
    Fenrir::Entity hidden = test.AddSquare({0.0f, 0.0f, -4.0f});

    test.RunFrame();
    INFO("the first frame's words are stale, so both draw in the first phase and the second phase draws nothing");
    REQUIRE(RendererProbe::Culls(test.renderer).front().commandsPerPhase == 1);
    REQUIRE(test.Count(0) == 2);
    REQUIRE(test.Count(1) == 0);

    test.RunFrame();
    INFO("the next frame draws the wall in the first phase; the square, tested against the wall's depth, in neither");
    REQUIRE(test.Count(0) == 1);
    REQUIRE(test.Count(1) == 0);

    hidden.GetComponent<Fenrir::Transform>().pos = {2.0f, 0.0f, -4.0f};
    test.RunFrame();
    INFO("moved out from behind the wall, the square is drawn in the second phase of the same frame");
    REQUIRE(test.Count(0) == 1);
    REQUIRE(test.Count(1) == 1);

    test.RunFrame();
    INFO("and in the first phase of the next");
    REQUIRE(test.Count(0) == 2);
    REQUIRE(test.Count(1) == 0);

    hidden.GetComponent<Fenrir::Transform>().pos = {0.0f, 0.0f, -4.0f};
    test.RunFrame();
    INFO("back behind the wall, it still draws in the first phase this frame, as it was visible last frame");
    REQUIRE(test.Count(0) == 2);
    test.RunFrame();
    INFO("and leaves the first phase the frame after");
    REQUIRE(test.Count(0) == 1);
    REQUIRE(test.Count(1) == 0);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("GL: drawnByPhase reads back how many each phase drew, FramesInFlight frames later",
          "[renderer][gl][culling][occlusion][readback]")
{
    GlOcclusionScene test;
    test.RequireOpenGL();
    test.AddSquare({0.0f, 0.0f, -3.0f}, 5.0f);
    Fenrir::Entity hidden = test.AddSquare({0.0f, 0.0f, -4.0f});

    test.RunFrame();
    test.RunFrame();
    hidden.GetComponent<Fenrir::Transform>().pos = {2.0f, 0.0f, -4.0f};
    test.RunFrame();
    const std::uint32_t frames = RendererProbe::Device(test.renderer).FramesInFlight();
    for (std::uint32_t frame = 0; frame < frames; ++frame)
    {
        // The square stays where it is, so every later frame draws both in the first phase.
        test.RunFrame();
    }
    INFO("the frame the square came out in drew one in each phase");
    const Fenrir::RendererStats stats = test.renderer.Stats();
    REQUIRE(stats.instancesDrawn == 2);
    REQUIRE(stats.drawnByPhase[0] == 1);
    REQUIRE(stats.drawnByPhase[1] == 1);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("GL: frames with occlusion culling on and off are the same, pixel for pixel, as the camera moves through "
          "walls and copies",
          "[renderer][gl][culling][occlusion][pixels]")
{
    GlOcclusionScene test;
    test.RequireOpenGL();
    // Three walls at different depths, partly overlapping, and a list of small copies in front of, between and
    // behind them, some wholly hidden, some half, some clear.
    test.AddSquare({-0.6f, 0.2f, -3.0f}, 3.0f);
    test.AddSquare({0.8f, -0.3f, -4.0f}, 4.0f);
    test.AddSquare({0.0f, 0.0f, -6.0f}, 12.0f);
    Fenrir::InstanceListData list;
    const std::array<Vec3, 8> places{Vec3(-0.6f, 0.2f, -3.5f), Vec3(0.8f, -0.3f, -5.0f), Vec3(0.0f, 0.0f, -7.0f),
                                     Vec3(-0.2f, 0.9f, -2.0f), Vec3(1.2f, 0.9f, -3.5f),  Vec3(-1.3f, -0.8f, -5.0f),
                                     Vec3(0.3f, 0.4f, -2.5f),  Vec3(-0.8f, -0.9f, -3.5f)};
    const std::array<Fenrir::Colour, 8> colours{
        Fenrir::Colour{1.0f, 0.0f, 0.0f, 1.0f}, Fenrir::Colour{0.0f, 1.0f, 0.0f, 1.0f},
        Fenrir::Colour{0.0f, 0.0f, 1.0f, 1.0f}, Fenrir::Colour{1.0f, 1.0f, 0.0f, 1.0f},
        Fenrir::Colour{0.0f, 1.0f, 1.0f, 1.0f}, Fenrir::Colour{1.0f, 0.0f, 1.0f, 1.0f},
        Fenrir::Colour{0.5f, 0.5f, 0.5f, 1.0f}, Fenrir::Colour{1.0f, 0.5f, 0.0f, 1.0f}};
    for (std::size_t i = 0; i < places.size(); ++i)
    {
        list.transforms.push_back(Fenrir::Math::Translate(Fenrir::Math::Mat4(1.0f), places[i]));
        list.colours.push_back(colours[i]);
    }
    const auto handle = test.assets.Store().Add<Fenrir::InstanceList>(std::move(list), Fenrir::AssetOwner::App());
    test.app.GetActiveScene().GetEntityList().CreateEntity().AddComponent<Fenrir::InstancedMeshRenderer>(
        Fenrir::InstancedMeshRenderer{test.square, test.white, handle, true, true});

    for (int frame = 0; frame < 6; ++frame)
    {
        CAPTURE(frame);
        // The camera slides right and turns left a little each frame, so copies leave the walls' cover and enter it.
        Fenrir::Transform& eye = test.camera.GetComponent<Fenrir::Transform>();
        eye.pos = {0.3f * static_cast<float>(frame), 0.1f * static_cast<float>(frame % 3), 0.0f};
        eye.rot = Fenrir::Math::AngleAxis(0.05f * static_cast<float>(frame), Vec3(0.0f, 1.0f, 0.0f));
        const GlProbe::Image culled = test.Frame();
        test.renderer.SetOcclusionCulling(false);
        const GlProbe::Image direct = test.Frame();
        test.renderer.SetOcclusionCulling(true);
        INFO("the culled frame shows what the direct one shows");
        RequireSame(culled, direct);
    }
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("GL: each level of a camera's Hi-Z pyramid holds the farthest depth it covers beside the nearest",
          "[renderer][gl][culling][occlusion][ssr]")
{
    GlTest::GlScene test;
    test.RequireOpenGL();
    constexpr float Near = 0.1f;
    constexpr float Far = 100.0f;
    // An orthographic camera one world unit a pixel of the 64 x 64 window: a wall at distance 10 over the left half
    // of the view, nothing over the right, and a 2 x 2 pixel square at distance 1 on the centre.
    Fenrir::Camera& camera = test.AddCamera();
    camera.projection = Fenrir::Orthographic{static_cast<float>(GlTest::GlScene::Size), Near, Far};
    camera.background = Fenrir::ColourBackground{Fenrir::Colour::Black};
    Fenrir::AssetStore& store = test.assets.Store();
    const auto grey = store.CreateMaterial(
        store.Load<Fenrir::Shader>("builtin:unlit", Fenrir::AssetOwner::App()).handle, Fenrir::AssetOwner::App());
    const auto wall = store.Add<Fenrir::Mesh>(RendererTest::Quad(64.0f, -10.0f), Fenrir::AssetOwner::App());
    Fenrir::Entity left = test.app.GetActiveScene().GetEntityList().CreateEntity();
    left.GetComponent<Fenrir::Transform>().pos = {-32.0f, 0.0f, 0.0f};
    left.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{wall, grey, true});
    test.AddQuad(2.0f, 1.0f, "builtin:unlit", Fenrir::Colour{1.0f, 1.0f, 1.0f, 1.0f});
    const auto depthAt = [&](float distance) { return (Far - distance) / (Far - Near); };
    const float near = depthAt(1.0f);
    const float far = depthAt(10.0f);

    test.RunFrame();
    test.RunFrame();
    const auto kept = RendererProbe::Reflections(test.renderer);
    REQUIRE(kept.size() == 1);
    GraphicsDevice& device = RendererProbe::Device(test.renderer);
    const auto level = [&](std::uint32_t mip) { return GlProbe::Read(device, kept[0].pyramid, mip); };

    const GlProbe::Image full = level(0);
    INFO("level 0 holds each pixel's depth as both: the square's on it, the wall's left of it, 0 right of it");
    REQUIRE(Close(full.At(31, 31)[0], near, 1e-5f));
    REQUIRE(Close(full.At(31, 31)[1], near, 1e-5f));
    REQUIRE(Close(full.At(20, 31)[0], far, 1e-5f));
    REQUIRE(Close(full.At(20, 31)[1], far, 1e-5f));
    REQUIRE(full.At(40, 31)[0] == 0.0f);
    REQUIRE(full.At(40, 31)[1] == 0.0f);

    const GlProbe::Image half = level(1);
    INFO("level 1's texel over the square and the wall takes the square's depth as nearest and the wall's as farthest");
    REQUIRE(Close(half.At(15, 15)[0], near, 1e-5f));
    REQUIRE(Close(half.At(15, 15)[1], far, 1e-5f));
    INFO("one over the square and nothing takes the square's and 0");
    REQUIRE(Close(half.At(16, 16)[0], near, 1e-5f));
    REQUIRE(half.At(16, 16)[1] == 0.0f);
    INFO("and one over the wall alone the wall's twice");
    REQUIRE(Close(half.At(10, 15)[0], far, 1e-5f));
    REQUIRE(Close(half.At(10, 15)[1], far, 1e-5f));

    const GlProbe::Image last = level(6);
    INFO("the last level, 1 x 1, has the square's depth as nearest and 0 as farthest, since nothing covers the right");
    REQUIRE((last.width == 1 && last.height == 1));
    REQUIRE(Close(last.At(0, 0)[0], near, 1e-5f));
    REQUIRE(last.At(0, 0)[1] == 0.0f);
    REQUIRE(test.messages->errors.empty());
}
