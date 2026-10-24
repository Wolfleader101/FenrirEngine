// SpawnNodes (ADR-0028, ADR-0031): a model's node tree as entities, under a given parent.
#include <cmath>
#include <cstddef>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "AssetsTest.hpp"
#include "FenrirAssets/Model.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirScene/Scene.hpp"

using AssetsTest::AssetsApp;
using Fenrir::AssetOwner;
using Fenrir::Entity;

namespace
{
    std::string NameOf(const Entity& entity)
    {
        return entity.GetComponent<Fenrir::Name>().Get();
    }

    Entity ParentOf(const Entity& entity)
    {
        return entity.GetComponent<Fenrir::Relationship>().parent;
    }

    bool Near(const Fenrir::Math::Vec3& a, const Fenrir::Math::Vec3& b)
    {
        return std::abs(a.x - b.x) < 1e-4f && std::abs(a.y - b.y) < 1e-4f && std::abs(a.z - b.z) < 1e-4f;
    }
} // namespace

TEST_CASE("SpawnNodes gives one entity per node, with names, local transforms and parents", "[assets][spawn]")
{
    AssetsApp test;
    Fenrir::Scene& level = test.app.CreateScene("Level");
    const Fenrir::LoadResult<Fenrir::Model> model = test.store.Load<Fenrir::Model>("models/scene.gltf", level);
    REQUIRE(model.Ok());

    Entity holder = level.GetEntityList().CreateEntity();
    holder.GetComponent<Fenrir::Transform>().pos = {10.0f, 0.0f, 0.0f};
    const Fenrir::SpawnedNodes spawned = test.store.SpawnNodes(level, model.handle, holder);

    INFO("the root holds the model under the given parent, named after its file");
    REQUIRE(spawned.root.IsValid());
    REQUIRE(ParentOf(spawned.root) == holder);
    REQUIRE(NameOf(spawned.root) == "scene");

    INFO("nodes is indexed by model node: Root, Panel, Marker, Empty");
    REQUIRE(spawned.nodes.size() == 4);
    REQUIRE(NameOf(spawned.nodes[0]) == "Root");
    REQUIRE(NameOf(spawned.nodes[1]) == "Panel");
    REQUIRE(NameOf(spawned.nodes[2]) == "Marker");
    REQUIRE(NameOf(spawned.nodes[3]) == "Empty");

    INFO("each entity's parent is its node's parent, and the model's root node hangs from the root");
    REQUIRE(ParentOf(spawned.nodes[0]) == spawned.root);
    REQUIRE(ParentOf(spawned.nodes[1]) == spawned.nodes[0]);
    REQUIRE(ParentOf(spawned.nodes[2]) == spawned.nodes[0]);
    REQUIRE(ParentOf(spawned.nodes[3]) == spawned.nodes[2]);

    INFO("each Transform is the node's local rest transform");
    REQUIRE(Near(spawned.nodes[0].GetComponent<Fenrir::Transform>().pos, {1.0f, 2.0f, 3.0f}));
    REQUIRE(Near(spawned.nodes[0].GetComponent<Fenrir::Transform>().scale, {2.0f, 2.0f, 2.0f}));
    REQUIRE(Near(spawned.nodes[3].GetComponent<Fenrir::Transform>().pos, {0.5f, 0.0f, 0.0f}));

    // Worked by hand: holder (10, 0, 0); Root (1, 2, 3), a quarter turn about +Y, scale 2; Marker (0, 0, -1) becomes
    // (-2, 0, 0) after the turn and scale; Empty (0.5, 0, 0) becomes (0, 0, -1). Sum: (9, 2, 2).
    INFO("so the world position of Empty composes every parent: (9, 2, 2)");
    const Fenrir::Math::Mat4 world = Fenrir::GetWorldMatrix(spawned.nodes[3]);
    REQUIRE(Near(Fenrir::Math::Vec3(world[3]), {9.0f, 2.0f, 2.0f}));
}

TEST_CASE("SpawnNodes with no parent puts the root at the top of the scene", "[assets][spawn]")
{
    AssetsApp test;
    Fenrir::Scene& level = test.app.CreateScene("Level");
    const Fenrir::LoadResult<Fenrir::Model> model = test.store.Load<Fenrir::Model>("models/scene.obj", level);
    REQUIRE(model.Ok());

    const Fenrir::SpawnedNodes spawned = test.store.SpawnNodes(level, model.handle);

    INFO("the root has no parent, and both of the OBJ's nodes hang from it");
    REQUIRE_FALSE(ParentOf(spawned.root).IsValid());
    REQUIRE(spawned.nodes.size() == 2);
    REQUIRE(ParentOf(spawned.nodes[0]) == spawned.root);
    REQUIRE(ParentOf(spawned.nodes[1]) == spawned.root);
}

TEST_CASE("SpawnNodes on a stale model spawns nothing and logs", "[assets][spawn]")
{
    AssetsApp test;
    Fenrir::Scene& level = test.app.CreateScene("Level");
    const Fenrir::LoadResult<Fenrir::Model> model =
        test.store.Load<Fenrir::Model>("models/scene.gltf", AssetOwner::App());
    REQUIRE(test.store.Unload(model.handle));

    const Fenrir::SpawnedNodes spawned = test.store.SpawnNodes(level, model.handle);

    INFO("no entity is made, and the error is logged");
    REQUIRE_FALSE(spawned.root.IsValid());
    REQUIRE(spawned.nodes.empty());
    REQUIRE(test.errors->size() == 1);
}
