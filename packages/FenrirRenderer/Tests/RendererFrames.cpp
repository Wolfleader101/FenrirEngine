// The facade's frame on the null backend (the spec's seam 1): minimised windows, a scene with no camera, read-back
// and installing.

#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "RendererTestApp.hpp"

using Fenrir::RendererProbe;
using RendererTest::RendererTestApp;

TEST_CASE("A minimised window submits nothing while the frame index advances", "[renderer][facade]")
{
    RendererTestApp test;
    test.AddCamera();
    test.AddMeshRenderer(test.Mesh(), test.Material());
    test.Frame();
    const std::uint64_t presents = RendererProbe::Presents(test.Device());
    const std::uint64_t frameIndex = RendererProbe::Core(test.renderer).FrameIndex();

    RendererProbe::SetSurfaceSize(test.renderer, {0, 0});
    test.Frame();

    INFO("nothing was submitted or presented");
    REQUIRE(RendererProbe::Submitted(test.renderer).empty());
    REQUIRE(RendererProbe::Presents(test.Device()) == presents);
    INFO("but the frame index moved on");
    REQUIRE(RendererProbe::Core(test.renderer).FrameIndex() == frameIndex + 1);
}

TEST_CASE("A scene with no camera clears the window to black, logged once", "[renderer][facade]")
{
    RendererTestApp test;
    test.AddMeshRenderer(test.Mesh(), test.Material());

    test.Frame();
    test.Frame();
    const std::vector<Fenrir::Graphics::CommandList>& lists = RendererProbe::Submitted(test.renderer);

    INFO("the one pass clears the backbuffer");
    REQUIRE(RendererTest::PassesIn(lists) == std::vector<std::string>{"Clear"});
    const auto& begin = std::get<Fenrir::Graphics::CmdBeginPass>(lists.at(0).commands.at(0));
    REQUIRE(begin.pass.colour.at(0).texture == test.Device().Backbuffer());
    REQUIRE(begin.pass.colour.at(0).load == Fenrir::Graphics::LoadOp::Clear);
    INFO("to black");
    REQUIRE(begin.pass.colour.at(0).clear == std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f});
    INFO("the frame is presented");
    REQUIRE(RendererProbe::Presents(test.Device()) == 2);
    INFO("the missing camera was logged once over two frames");
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "no enabled Camera") == 1);
}

TEST_CASE("A disabled camera draws no view", "[renderer][facade]")
{
    RendererTestApp test;
    test.AddCamera().GetComponent<Fenrir::Camera>().enabled = false;
    test.AddMeshRenderer(test.Mesh(), test.Material());

    test.Frame();

    INFO("the window is cleared, and nothing drawn");
    REQUIRE(RendererTest::PassesIn(RendererProbe::Submitted(test.renderer)) == std::vector<std::string>{"Clear"});
}

TEST_CASE("GetWorldBounds is the mesh's local box moved by the entity's world transform", "[renderer][facade]")
{
    RendererTestApp test;
    Fenrir::Entity cube = test.AddMeshRenderer(test.Mesh(RendererTest::Cube()), test.Material());
    cube.GetComponent<Fenrir::Transform>().pos = {1.0f, 2.0f, 3.0f};
    cube.GetComponent<Fenrir::Transform>().scale = {2.0f, 2.0f, 2.0f};
    Fenrir::Entity bare = test.ActiveScene().GetEntityList().CreateEntity();

    const std::optional<Fenrir::Math::AABB> bounds = test.renderer.GetWorldBounds(cube);

    INFO("a unit cube scaled by 2 and moved to (1, 2, 3) spans (0, 1, 2) to (2, 3, 4)");
    REQUIRE(bounds.has_value());
    REQUIRE(bounds->min == Fenrir::Math::Vec3(0.0f, 1.0f, 2.0f));
    REQUIRE(bounds->max == Fenrir::Math::Vec3(2.0f, 3.0f, 4.0f));
    INFO("an entity with no MeshRenderer has none");
    REQUIRE_FALSE(test.renderer.GetWorldBounds(bare).has_value());
}

TEST_CASE("Stats report the last frame's draws, passes and uploads", "[renderer][facade]")
{
    RendererTestApp test;
    test.AddCamera();
    const auto mesh = test.Mesh();
    const auto material = test.Material();
    test.AddMeshRenderer(mesh, material);
    test.AddMeshRenderer(mesh, material);

    INFO("before any frame, every count is zero");
    REQUIRE(test.renderer.Stats().draws == 0);
    test.Frame();
    const Fenrir::RendererStats stats = test.renderer.Stats();

    INFO("two MeshRenderers are two draws");
    REQUIRE(stats.draws == 2);
    INFO("one view runs Opaque, Background and Output");
    REQUIRE(stats.passes == 3);
    INFO("the shared mesh and material were each uploaded once");
    REQUIRE(stats.uploads == 2);
}

TEST_CASE("Installing on the null backend needs no window", "[renderer][facade][install]")
{
    RendererTestApp test;

    INFO("the owner is valid, and nothing was logged as an error");
    REQUIRE(test.renderer.IsValid());
    REQUIRE(test.messages->errors.empty());
    INFO("vsync can be set, every mode supported");
    REQUIRE(test.renderer.SetVsync(Fenrir::Vsync::Adaptive) == Fenrir::Vsync::Adaptive);
}

TEST_CASE("Installing on OpenGL without a window gives an invalid owner whose calls do nothing",
          "[renderer][facade][install]")
{
    Fenrir::RendererConfig config;
    config.backend = Fenrir::GraphicsBackend::OpenGL;
    RendererTestApp test(config);
    Fenrir::Entity entity = test.AddMeshRenderer(test.Mesh(), {});

    INFO("the owner is invalid, and why was logged once");
    REQUIRE_FALSE(test.renderer.IsValid());
    REQUIRE(test.messages->errors.size() == 1);
    INFO("a frame runs, and its calls do nothing");
    test.Frame();
    REQUIRE(test.renderer.Stats().draws == 0);
    REQUIRE_FALSE(test.renderer.GetWorldBounds(entity).has_value());
    REQUIRE(test.renderer.SetVsync(Fenrir::Vsync::On) == Fenrir::Vsync::Off);
}

TEST_CASE("Installing without FenrirAssets gives an invalid owner", "[renderer][facade][install]")
{
    auto messages = std::make_shared<RendererTest::LoggedMessages>();
    Fenrir::App app(std::make_unique<RendererTest::RecordingLogger>(messages), 1);

    const Fenrir::Renderer renderer = Fenrir::Renderer::Install(app, RendererTest::NullBackend());

    INFO("the owner is invalid, and why was logged once");
    REQUIRE_FALSE(renderer.IsValid());
    REQUIRE(messages->errors.size() == 1);
}
