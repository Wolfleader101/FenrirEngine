// Automatic instancing on the null backend through App::RunFrame (the spec's seam 1, ADR-0051): which opaque and
// masked items merge into one instanced draw in DepthPrepass and Opaque, what splits a run, transparent items staying
// single, RendererStats counting draws and instances apart, and RendererConfig turning merging off.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "RendererTestApp.hpp"

using RendererTest::RecordedDraw;
using RendererTest::RendererTestApp;

namespace
{
    /**
     * @brief `count` MeshRenderers of `mesh` and `material`, along -Z a unit apart at `x`: in front of the camera at
     * (0, 0, 5), which has no far plane, so every one is inside its frustum, whatever the count (ADR-0057's coarse
     * cull leaves an item outside the view out of its draws).
     */
    void AddRow(RendererTestApp& test, std::size_t count, Fenrir::AssetHandle<Fenrir::Mesh> mesh,
                Fenrir::AssetHandle<Fenrir::Material> material, float x = 0.0f)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            Fenrir::Entity entity = test.AddMeshRenderer(mesh, material);
            entity.GetComponent<Fenrir::Transform>().pos = {x, 0.0f, -static_cast<float>(i)};
        }
    }

    /** @brief The instances of every draw, summed. */
    std::uint32_t Instances(const std::vector<RecordedDraw>& draws)
    {
        std::uint32_t instances = 0;
        for (const RecordedDraw& draw : draws)
        {
            instances += draw.draw.instanceCount;
        }
        return instances;
    }

    /** @brief A builtin:lit material with `mode`. */
    Fenrir::AssetHandle<Fenrir::Material> Blended(RendererTestApp& test, Fenrir::BlendMode mode)
    {
        const auto material = test.Material();
        test.Store().SetBlendMode(material, mode);
        return material;
    }

    Fenrir::RendererConfig WithoutInstancing()
    {
        Fenrir::RendererConfig config = RendererTest::NullBackend();
        config.automaticInstancing = false;
        return config;
    }
} // namespace

TEST_CASE("A thousand identical cubes are one draw of a thousand instances in the prepass and Opaque",
          "[renderer][instancing]")
{
    RendererTestApp test;
    test.AddCamera();
    AddRow(test, 1000, test.Mesh(), test.Material());

    test.Frame();
    for (const char* pass : {"DepthPrepass", "Opaque"})
    {
        const std::vector<RecordedDraw> draws = test.Draws(pass);
        INFO(pass << " draws them once, a thousand instances from the first record");
        REQUIRE(draws.size() == 1);
        REQUIRE(draws.front().draw.instanceCount == 1000);
        REQUIRE(draws.front().draw.firstInstance == 0);
    }
    INFO("the stats count one draw of a thousand instances");
    const Fenrir::RendererStats stats = test.renderer.Stats();
    REQUIRE(stats.draws == 1);
    REQUIRE(stats.instances == 1000);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A different material or mesh splits a run, and each run starts at its first record",
          "[renderer][instancing]")
{
    RendererTestApp test;
    test.AddCamera();
    const auto mesh = test.Mesh();
    AddRow(test, 300, mesh, test.Material());
    AddRow(test, 200, mesh, test.Material(), 1.0f);
    AddRow(test, 100, test.Mesh(), test.Material("builtin:unlit"), 2.0f);

    test.Frame();
    const std::vector<RecordedDraw> opaque = test.Draws("Opaque");
    INFO("three runs, one per material and mesh, covering every item once");
    REQUIRE(opaque.size() == 3);
    REQUIRE(Instances(opaque) == 600);
    std::vector<std::uint32_t> counts;
    std::uint32_t next = 0;
    for (const RecordedDraw& draw : opaque)
    {
        counts.push_back(draw.draw.instanceCount);
        INFO("the runs take consecutive records of the draw buffer");
        REQUIRE(draw.draw.firstInstance == next);
        next += draw.draw.instanceCount;
    }
    std::ranges::sort(counts);
    REQUIRE(counts == std::vector<std::uint32_t>{100, 200, 300});
    INFO("the prepass merges the same runs");
    REQUIRE(test.Draws("DepthPrepass").size() == 3);
    REQUIRE(test.renderer.Stats().draws == 3);
    REQUIRE(test.renderer.Stats().instances == 600);
}

TEST_CASE("Masked items merge, and transparent items stay one draw each", "[renderer][instancing]")
{
    RendererTestApp test;
    test.AddCamera();
    const auto mesh = test.Mesh();
    AddRow(test, 10, mesh, Blended(test, Fenrir::BlendMode::Masked));
    // Inside the camera's frustum: an item outside it is not drawn at all (ADR-0057).
    AddRow(test, 10, mesh, Blended(test, Fenrir::BlendMode::AlphaBlend), -2.0f);

    test.Frame();
    INFO("the masked items are one draw in the prepass and in Opaque");
    REQUIRE(test.Draws("DepthPrepass").size() == 1);
    REQUIRE(test.Draws("Opaque").size() == 1);
    REQUIRE(test.Draws("Opaque").front().draw.instanceCount == 10);
    INFO("the transparent ones are ten draws of one instance, sorted by depth as before");
    const std::vector<RecordedDraw> transparent = test.Draws("Transparent");
    REQUIRE(transparent.size() == 10);
    REQUIRE(Instances(transparent) == 10);
    INFO("the stats count eleven draws of twenty instances");
    REQUIRE(test.renderer.Stats().draws == 11);
    REQUIRE(test.renderer.Stats().instances == 20);
}

TEST_CASE("Turning automatic instancing off draws each item on its own", "[renderer][instancing]")
{
    RendererTestApp test(WithoutInstancing());
    test.AddCamera();
    AddRow(test, 1000, test.Mesh(), test.Material());

    test.Frame();
    for (const char* pass : {"DepthPrepass", "Opaque"})
    {
        const std::vector<RecordedDraw> draws = test.Draws(pass);
        INFO(pass << " draws each cube once, one instance at its own record");
        REQUIRE(draws.size() == 1000);
        for (std::size_t i = 0; i < draws.size(); ++i)
        {
            REQUIRE(draws[i].draw.instanceCount == 1);
            REQUIRE(draws[i].draw.firstInstance == i);
        }
    }
    REQUIRE(test.renderer.Stats().draws == 1000);
    REQUIRE(test.renderer.Stats().instances == 1000);
}
