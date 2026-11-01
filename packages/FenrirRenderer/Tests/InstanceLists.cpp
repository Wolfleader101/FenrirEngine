// InstancedMeshRenderer on the null backend through App::RunFrame (the spec's seam 1, ADR-0051): an instance list is
// uploaded when it is new or its revision moves on, and never again otherwise, however its entity moves; and a list of
// any size is one draw a pass, never merged with other items.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirAssets/InstanceListData.hpp"
#include "RendererTestApp.hpp"

using RendererTest::RecordedDraw;
using RendererTest::RendererTestApp;

namespace
{
    /** @brief `count` copies along +X, a unit apart. */
    Fenrir::InstanceListData Row(std::size_t count)
    {
        Fenrir::InstanceListData list;
        list.transforms.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            list.transforms.push_back(
                Fenrir::Math::Translate(Fenrir::Math::Mat4(1.0f), {static_cast<float>(i), 0.0f, 0.0f}));
        }
        return list;
    }

    /** @brief An entity drawing `list` with a cube in a builtin:lit material. */
    Fenrir::Entity AddInstanced(RendererTestApp& test, Fenrir::AssetHandle<Fenrir::InstanceList> list,
                                Fenrir::AssetHandle<Fenrir::Mesh> mesh, Fenrir::AssetHandle<Fenrir::Material> material)
    {
        Fenrir::Entity entity = test.ActiveScene().GetEntityList().CreateEntity();
        entity.AddComponent<Fenrir::InstancedMeshRenderer>(
            Fenrir::InstancedMeshRenderer{mesh, material, list, true, true});
        return entity;
    }
} // namespace

TEST_CASE("An unchanged instance list uploads once, and an edit uploads it again", "[renderer][instancing]")
{
    RendererTestApp test;
    test.AddCamera();
    const auto mesh = test.Mesh();
    const auto material = test.Material();
    const auto list = test.Store().Add<Fenrir::InstanceList>(Row(50), Fenrir::AssetOwner::App());
    Fenrir::Entity entity = AddInstanced(test, list, mesh, material);

    test.Frame();
    INFO("the first frame uploads the list, with the mesh and the material");
    REQUIRE(test.renderer.Stats().uploads >= 1);
    REQUIRE(test.renderer.Stats().instances == 50);

    for (int frame = 0; frame < 3; ++frame)
    {
        entity.GetComponent<Fenrir::Transform>().pos.y += 1.0f;
        test.Frame();
        INFO("frame " << frame << " uploads nothing, though the entity moved");
        REQUIRE(test.renderer.Stats().uploads == 0);
        REQUIRE(test.renderer.Stats().instances == 50);
    }

    REQUIRE(test.Store().Replace(list, Row(80)));
    test.Frame();
    INFO("replacing the list bumps its revision, so it is uploaded again, and draws its new copies");
    REQUIRE(test.renderer.Stats().uploads == 1);
    REQUIRE(test.renderer.Stats().instances == 80);
    REQUIRE(test.Draws("Opaque").front().draw.instanceCount == 80);

    test.Frame();
    INFO("and then not again");
    REQUIRE(test.renderer.Stats().uploads == 0);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A hundred thousand copies are one draw in each pass", "[renderer][instancing]")
{
    RendererTestApp test;
    test.AddCamera();
    const auto list = test.Store().Add<Fenrir::InstanceList>(Row(100000), Fenrir::AssetOwner::App());
    AddInstanced(test, list, test.Mesh(), test.Material());

    test.Frame();
    for (const char* pass : {"DepthPrepass", "Opaque"})
    {
        const std::vector<RecordedDraw> draws = test.Draws(pass);
        INFO(pass << " draws the list once, a hundred thousand instances of its one record");
        REQUIRE(draws.size() == 1);
        REQUIRE(draws.front().draw.instanceCount == 100000);
        REQUIRE(draws.front().draw.firstInstance == 0);
    }
    INFO("the stats count one draw of a hundred thousand instances");
    REQUIRE(test.renderer.Stats().draws == 1);
    REQUIRE(test.renderer.Stats().instances == 100000);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Instance lists never merge with each other or with MeshRenderers of the same mesh and material",
          "[renderer][instancing]")
{
    RendererTestApp test;
    test.AddCamera();
    const auto mesh = test.Mesh();
    const auto material = test.Material();
    AddInstanced(test, test.Store().Add<Fenrir::InstanceList>(Row(10), Fenrir::AssetOwner::App()), mesh, material);
    AddInstanced(test, test.Store().Add<Fenrir::InstanceList>(Row(20), Fenrir::AssetOwner::App()), mesh, material);
    for (int i = 0; i < 5; ++i)
    {
        test.AddMeshRenderer(mesh, material);
    }

    test.Frame();
    const std::vector<RecordedDraw> opaque = test.Draws("Opaque");
    INFO("each list is a draw of its own copies, and the five MeshRenderers one merged draw");
    std::vector<std::uint32_t> counts;
    for (const RecordedDraw& draw : opaque)
    {
        counts.push_back(draw.draw.instanceCount);
    }
    std::ranges::sort(counts);
    REQUIRE(counts == std::vector<std::uint32_t>{5, 10, 20});
    REQUIRE(test.renderer.Stats().draws == 3);
    REQUIRE(test.renderer.Stats().instances == 35);
}

TEST_CASE("A stale instance list draws nothing and is released", "[renderer][instancing]")
{
    RendererTestApp test;
    test.AddCamera();
    const auto list = test.Store().Add<Fenrir::InstanceList>(Row(10), Fenrir::AssetOwner::App());
    AddInstanced(test, list, test.Mesh(), test.Material());
    test.Frame();
    REQUIRE(test.Draws("Opaque").size() == 1);

    REQUIRE(test.Store().Unload(list));
    test.Frame();
    INFO("its entity is no longer drawn, with one warning naming the stale list");
    REQUIRE(test.renderer.Stats().instances == 0);
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "stale instance list") == 1);
    test.Frame();
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "stale instance list") == 1);
}
