// The prepare step on the null backend (the spec's seam 1): material blocks from parameter defaults and overrides,
// uploads on a new revision, and GPU resources freed when a handle goes stale.

#include <cmath>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "RendererTestApp.hpp"

using Fenrir::RendererProbe;
using RendererTest::FloatAt;
using RendererTest::RendererTestApp;

namespace
{
    // builtin:lit's MaterialParams at std140 offsets: vec4 baseColour, vec4 emissive, float metallic, float roughness.
    constexpr std::size_t BaseColour = 0;
    constexpr std::size_t Emissive = 16;
    constexpr std::size_t Metallic = 32;
    constexpr std::size_t Roughness = 36;
} // namespace

TEST_CASE("A material's uploaded block holds its shader's parameter defaults", "[renderer][facade][materials]")
{
    RendererTestApp test;
    test.AddCamera();
    test.AddMeshRenderer(test.Mesh(), test.Material("builtin:lit"));

    test.Frame();
    const std::vector<RendererTest::RecordedDraw> draws = test.Draws();
    REQUIRE(draws.size() == 1);
    const std::vector<std::byte> block = test.MaterialBlock(draws[0]);

    INFO("baseColour defaults to white, by builtin:lit's pragma");
    REQUIRE(FloatAt(block, BaseColour) == 1.0f);
    REQUIRE(FloatAt(block, BaseColour + 4) == 1.0f);
    REQUIRE(FloatAt(block, BaseColour + 8) == 1.0f);
    REQUIRE(FloatAt(block, BaseColour + 12) == 1.0f);
    INFO("emissive to opaque black");
    REQUIRE(FloatAt(block, Emissive) == 0.0f);
    REQUIRE(FloatAt(block, Emissive + 12) == 1.0f);
    INFO("metallic to 0 and roughness to 0.5");
    REQUIRE(FloatAt(block, Metallic) == 0.0f);
    REQUIRE(FloatAt(block, Roughness) == 0.5f);
}

TEST_CASE("A parameter override replaces only its member in the uploaded block", "[renderer][facade][materials]")
{
    RendererTestApp test;
    test.AddCamera();
    const Fenrir::AssetHandle<Fenrir::Material> material = test.Material("builtin:lit");
    test.AddMeshRenderer(test.Mesh(), material);
    test.Frame();

    test.Store().SetFloat(material, "roughness", 0.8f);
    test.Store().SetColour(material, "baseColour", Fenrir::Colour::Red);
    test.Frame();
    const std::vector<RendererTest::RecordedDraw> draws = test.Draws();
    REQUIRE(draws.size() == 1);
    const std::vector<std::byte> block = test.MaterialBlock(draws[0]);

    INFO("the overridden members hold their overrides");
    REQUIRE(std::abs(FloatAt(block, Roughness) - 0.8f) < 1e-6f);
    REQUIRE(FloatAt(block, BaseColour) == 1.0f);
    REQUIRE(FloatAt(block, BaseColour + 4) == 0.0f);
    REQUIRE(FloatAt(block, BaseColour + 8) == 0.0f);
    INFO("the others still hold their defaults");
    REQUIRE(FloatAt(block, Metallic) == 0.0f);
    REQUIRE(FloatAt(block, Emissive + 12) == 1.0f);
}

TEST_CASE("Replacing a mesh uploads it again once", "[renderer][facade][prepare]")
{
    RendererTestApp test;
    test.AddCamera();
    const Fenrir::AssetHandle<Fenrir::Mesh> mesh = test.Mesh(RendererTest::Cube());
    test.AddMeshRenderer(mesh, test.Material());

    test.Frame();
    INFO("the first frame uploads the mesh and the material's block");
    REQUIRE(test.renderer.Stats().uploads == 2);
    test.Frame();
    INFO("an unchanged frame uploads nothing");
    REQUIRE(test.renderer.Stats().uploads == 0);

    test.Store().Replace(mesh, RendererTest::Quad());
    test.Frame();
    INFO("the frame after a Replace uploads the new revision once");
    REQUIRE(test.renderer.Stats().uploads == 1);
    REQUIRE(test.Draws().at(0).draw.indexCount == 6);
    test.Frame();
    INFO("and not again");
    REQUIRE(test.renderer.Stats().uploads == 0);
}

TEST_CASE("Unloading a mesh frees its GPU buffers", "[renderer][facade][prepare]")
{
    RendererTestApp test;
    test.AddCamera();
    const Fenrir::AssetHandle<Fenrir::Mesh> mesh = test.Mesh();
    test.AddMeshRenderer(mesh, test.Material());
    test.Frame();
    const RendererTest::RecordedDraw drawn = test.Draws().at(0);
    REQUIRE(test.Device().IsLive(drawn.indexBuffer));

    test.Store().Unload(mesh);
    test.Frame();

    INFO("the mesh's index buffer is no longer live");
    REQUIRE_FALSE(test.Device().IsLive(drawn.indexBuffer));
    INFO("nor its vertex buffer");
    REQUIRE_FALSE(test.Device().IsLive(drawn.vertexBuffers.at(0)));
}

TEST_CASE("The built-in shaders are registered under builtin: names at install", "[renderer][facade]")
{
    RendererTestApp test;

    for (const char* name :
         {"builtin:unlit", "builtin:lit", "builtin:fallback", "builtin:background", "builtin:output"})
    {
        INFO(name << " loads from the asset store");
        REQUIRE(test.Store().Load<Fenrir::Shader>(name, Fenrir::AssetOwner::App()).Ok());
    }
    INFO("builtin:lit's parameters are the spec's, with their defaults");
    const auto lit = test.Builtin("builtin:lit");
    REQUIRE(test.Store().TryGetFloat(test.Material("builtin:lit"), "roughness").value_or(0.0f) == 0.5f);
    REQUIRE(test.Store().GetShaderParameters(lit).size() == 4);
}
