// The ECS facade on the null backend through App::RunFrame (the spec's seam 1): which draws reach the Opaque pass,
// with which pipeline and bindings, and in which order.

#include <map>
#include <set>
#include <span>
#include <tuple>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirShaders/WrongFrameSet.hpp"
#include "RendererTestApp.hpp"

using Fenrir::RendererProbe;
using RendererTest::RendererTestApp;

TEST_CASE("One MeshRenderer seen by one camera is one draw in Opaque with the lit pipeline and all four bindings",
          "[renderer][facade]")
{
    RendererTestApp test;
    test.AddCamera();
    test.AddMeshRenderer(test.Mesh(), test.Material("builtin:lit"));

    test.Frame();
    const std::vector<RendererTest::RecordedDraw> draws = test.Draws();

    INFO("the one MeshRenderer is one draw in the Opaque pass");
    REQUIRE(draws.size() == 1);
    INFO("it draws with builtin:lit's pipeline");
    REQUIRE(RendererProbe::Label(test.Device(), draws[0].pipeline) == "BuiltinLit");
    INFO("with the frame, view, material and draw binding groups at sets 0 to 3");
    REQUIRE(test.GroupLabel(draws[0], 0) == "Frame");
    REQUIRE(test.GroupLabel(draws[0], 1) == "View");
    REQUIRE(test.GroupLabel(draws[0], 2) == "Material");
    REQUIRE(test.GroupLabel(draws[0], 3) == "Draw");
    INFO("and all 36 of the cube's indices, as the first record of the draw buffer");
    REQUIRE(draws[0].draw.indexCount == 36);
    REQUIRE(draws[0].draw.firstInstance == 0);
    INFO("nothing it named was stale, and nothing was logged as an error");
    REQUIRE(RendererProbe::StaleUses(test.Device()).empty());
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A MeshRenderer that is not visible is not drawn", "[renderer][facade]")
{
    RendererTestApp test;
    // Without bloom, whose passes Bloom.cpp checks.
    test.AddCamera().GetComponent<Fenrir::Camera>().bloom.intensity = 0.0f;
    Fenrir::Entity entity = test.AddMeshRenderer(test.Mesh(), test.Material());
    entity.GetComponent<Fenrir::MeshRenderer>().visible = false;

    test.Frame();

    INFO("the Opaque pass draws nothing");
    REQUIRE(test.Draws().empty());
    INFO("the frame still ran its passes");
    REQUIRE(RendererTest::PassesIn(RendererProbe::Submitted(test.renderer)) ==
            std::vector<std::string>{"BrdfLut", "Opaque", "Background", "Output"});
}

TEST_CASE("A MeshRenderer naming a stale mesh is skipped, logged once", "[renderer][facade]")
{
    RendererTestApp test;
    test.AddCamera();
    const Fenrir::AssetHandle<Fenrir::Mesh> mesh = test.Mesh();
    test.AddMeshRenderer(mesh, test.Material());
    test.Store().Unload(mesh);

    test.Frame();
    test.Frame();

    INFO("nothing is drawn");
    REQUIRE(test.Draws().empty());
    INFO("the stale mesh was logged once over two frames");
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "stale mesh") == 1);
}

TEST_CASE("A MeshRenderer with no material draws with the fallback material, logged once", "[renderer][facade]")
{
    RendererTestApp test;
    test.AddCamera();
    test.AddMeshRenderer(test.Mesh(), {});

    test.Frame();
    test.Frame();
    const std::vector<RendererTest::RecordedDraw> draws = test.Draws();

    INFO("it is drawn, with the fallback material's pipeline");
    REQUIRE(draws.size() == 1);
    REQUIRE(RendererProbe::Label(test.Device(), draws[0].pipeline) == "BuiltinFallback");
    INFO("the missing material was logged once over two frames");
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "fallback material") == 1);
}

TEST_CASE("A MeshRenderer whose material went stale draws with the fallback material", "[renderer][facade]")
{
    RendererTestApp test;
    test.AddCamera();
    const Fenrir::AssetHandle<Fenrir::Material> material = test.Material();
    test.AddMeshRenderer(test.Mesh(), material);
    test.Frame();
    test.Store().Unload(material);

    test.Frame();
    const std::vector<RendererTest::RecordedDraw> draws = test.Draws();

    INFO("it is still drawn, with the fallback material's pipeline");
    REQUIRE(draws.size() == 1);
    REQUIRE(RendererProbe::Label(test.Device(), draws[0].pipeline) == "BuiltinFallback");
    INFO("nothing it named was stale");
    REQUIRE(RendererProbe::StaleUses(test.Device()).empty());
}

TEST_CASE("Opaque items are drawn grouped by pipeline, then material, then mesh", "[renderer][facade]")
{
    RendererTestApp test;
    test.AddCamera();
    const auto cube = test.Mesh(RendererTest::Cube());
    const auto quad = test.Mesh(RendererTest::Quad());
    const auto litA = test.Material("builtin:lit");
    const auto litB = test.Material("builtin:lit");
    const auto unlit = test.Material("builtin:unlit");
    // Spawned interleaved, so the order they are read in is not the order they draw in.
    test.AddMeshRenderer(cube, litA);
    test.AddMeshRenderer(quad, unlit);
    test.AddMeshRenderer(quad, litB);
    test.AddMeshRenderer(cube, unlit);
    test.AddMeshRenderer(quad, litA);
    test.AddMeshRenderer(cube, litB);
    test.AddMeshRenderer(cube, litA);
    test.AddMeshRenderer(quad, unlit);

    test.Frame();
    const std::vector<RendererTest::RecordedDraw> draws = test.Draws();

    INFO("every item is drawn once");
    REQUIRE(draws.size() == 8);
    // Each key is drawn in one unbroken run: a pipeline, a material within it, a mesh within that.
    const auto inOneRun = [&](auto keyOf) {
        std::set<decltype(keyOf(draws[0]))> finished;
        for (std::size_t i = 0; i < draws.size(); ++i)
        {
            const auto key = keyOf(draws[i]);
            if (finished.contains(key))
            {
                return false;
            }
            if (i + 1 == draws.size() || keyOf(draws[i + 1]) != key)
            {
                finished.insert(key);
            }
        }
        return true;
    };
    const auto pipeline = [](const RendererTest::RecordedDraw& d) {
        return std::tuple(d.pipeline.index, d.pipeline.generation);
    };
    const auto material = [](const RendererTest::RecordedDraw& d) {
        return std::tuple(d.pipeline.index, d.groups[2].index, d.groups[2].generation);
    };
    const auto mesh = [](const RendererTest::RecordedDraw& d) {
        return std::tuple(d.pipeline.index, d.groups[2].index, d.groups[2].generation, d.indexBuffer.index,
                          d.indexBuffer.generation);
    };
    INFO("the draws of each pipeline are together");
    REQUIRE(inOneRun(pipeline));
    INFO("within a pipeline, the draws of each material are together");
    REQUIRE(inOneRun(material));
    INFO("within a material, the draws of each mesh are together");
    REQUIRE(inOneRun(mesh));
    INFO("each draw reads its own record of the draw buffer");
    for (std::size_t i = 0; i < draws.size(); ++i)
    {
        REQUIRE(draws[i].draw.firstInstance == i);
    }
}

TEST_CASE("A vertex stream the shader reads but the mesh lacks binds a constant default, logged once",
          "[renderer][facade]")
{
    RendererTestApp test;
    test.AddCamera();
    // Positions and normals only: builtin:lit also reads vertex colours.
    test.AddMeshRenderer(test.Mesh(RendererTest::Cube()), test.Material("builtin:lit"));

    test.Frame();
    test.Frame();
    const std::vector<RendererTest::RecordedDraw> draws = test.Draws();

    INFO("it is drawn");
    REQUIRE(draws.size() == 1);
    INFO("the missing colours were logged once over two frames");
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "has no colours") == 1);
    INFO("one vertex slot reads a buffer other than the mesh's own");
    std::set<std::pair<std::uint32_t, std::uint32_t>> buffers;
    for (const Fenrir::Graphics::BufferHandle buffer : draws[0].vertexBuffers)
    {
        buffers.insert({buffer.index, buffer.generation});
    }
    REQUIRE(buffers.size() == 2);
}

TEST_CASE("A mesh without a tangent stream is drawn by builtin:lit with nothing logged about its tangents",
          "[renderer][facade]")
{
    RendererTestApp test;
    test.AddCamera();
    // Every stream builtin:lit reads but tangents.
    Fenrir::MeshData mesh = RendererTest::TexturedQuad();
    mesh.colours.assign(mesh.positions.size(), Fenrir::Colour::White);
    test.AddMeshRenderer(test.Mesh(std::move(mesh)), test.Material("builtin:lit"));

    test.Frame();

    INFO("it is drawn, and nothing is logged about its tangents");
    REQUIRE(test.Draws().size() == 1);
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "tangents") == 0);
}

TEST_CASE("A shader whose frame set disagrees with the renderer's fails its pipeline in debug builds",
          "[renderer][facade]")
{
    if (!Fenrir::DebugValidationByDefault)
    {
        SKIP("the fixed sets are checked against reflection in debug builds only");
    }
    RendererTestApp test;
    test.AddCamera();
    Fenrir::ShaderData shader;
    const auto bytes = std::as_bytes(std::span(Fenrir::Shaders::WrongFrameSet));
    shader.bytes.assign(bytes.begin(), bytes.end());
    const auto handle = test.Store().Add<Fenrir::Shader>(std::move(shader), Fenrir::AssetOwner::App());
    test.AddMeshRenderer(test.Mesh(), test.Store().CreateMaterial(handle, Fenrir::AssetOwner::App()));

    test.Frame();
    const std::vector<RendererTest::RecordedDraw> draws = test.Draws();

    INFO("the pipeline's failure is logged, naming the binding");
    REQUIRE(RendererTest::CountContaining(test.messages->errors, "set 0 binding 0") == 1);
    INFO("and the mesh is drawn with the fallback material instead");
    REQUIRE(draws.size() == 1);
    REQUIRE(RendererProbe::Label(test.Device(), draws[0].pipeline) == "BuiltinFallback");
}
