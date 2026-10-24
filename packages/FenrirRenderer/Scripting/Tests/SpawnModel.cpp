// SpawnModel from C# (spec seam 7: SpawnModel returning a usable root): recorded through the change buffer, its root
// usable in the same system, and the tree it makes the one C++'s SpawnModel makes. The fixture model is
// two_parts.gltf: a node "Body" with two meshes and a child node "Tip".

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirRenderer/SpawnModel.hpp"
#include "RendererScriptingApp.hpp"

using namespace Fenrir;
using namespace RendererScriptingTests;

namespace
{
    // One entity of a spawned tree, as a caller sees it: its name, place, what it draws, and its children, in order.
    struct Node
    {
        std::string name;
        Math::Vec3 position{0.0f};
        Math::Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        Math::Vec3 scale{1.0f};
        bool draws = false;
        AssetHandle<Mesh> mesh;
        AssetHandle<Material> material;
        std::vector<Node> children;
    };

    Node Describe(const Entity& entity)
    {
        Node node;
        node.name = entity.GetComponent<Name>().Get();
        const Transform& transform = entity.GetComponent<Transform>();
        node.position = transform.pos;
        node.rotation = transform.rot;
        node.scale = transform.scale;
        if (entity.HasComponent<MeshRenderer>())
        {
            node.draws = true;
            node.mesh = entity.GetComponent<MeshRenderer>().mesh;
            node.material = entity.GetComponent<MeshRenderer>().material;
        }
        entity.ForEachChild([&](const Entity& child) { node.children.push_back(Describe(child)); });
        return node;
    }

    // Whether two trees are the same, below their roots (whose places a caller sets).
    bool SameBelow(const Node& a, const Node& b)
    {
        if (a.name != b.name || a.draws != b.draws || a.mesh.index != b.mesh.index ||
            a.mesh.generation != b.mesh.generation || a.material.index != b.material.index ||
            a.material.generation != b.material.generation || a.children.size() != b.children.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < a.children.size(); ++i)
        {
            const Node& x = a.children[i];
            const Node& y = b.children[i];
            if (x.position != y.position || x.rotation != y.rotation || x.scale != y.scale || !SameBelow(x, y))
            {
                return false;
            }
        }
        return true;
    }
} // namespace

TEST_CASE("SpawnModel from C# returns a root usable in the same system, over the tree C++'s SpawnModel makes",
          "[renderer][scripting]")
{
    RendererScriptingApp test;
    const Entity parent = test.Entities().CreateEntity();
    test.Call<void>(ModelSpawnerType, "Arm", parent.GetId());
    test.Frame();

    {
        INFO("The root C# got should have been alive in the system that spawned it");
        REQUIRE(test.Call<bool>(ModelSpawnerType, "RootAliveAtOnce"));
    }
    const auto rootId = test.Call<std::uint32_t>(ModelSpawnerType, "Root");
    REQUIRE(test.Entities().HasEntity(rootId));
    const Entity root = test.Entities().GetEntity(rootId);
    {
        INFO("The Transform C# gave the root in the same system should have played back after the spawn's");
        REQUIRE(root.GetComponent<Transform>().pos == Math::Vec3(4.0f, 5.0f, 6.0f));
    }
    {
        INFO("The root should be the child of the parent C# named");
        REQUIRE(root.GetComponent<Relationship>().parent == parent);
    }

    const auto model = test.Store().Load<Model>("two_parts.gltf", AssetOwner::App()).handle;
    const Entity expected = SpawnModel(test.Store(), test.Get().GetActiveScene(), model);
    const Node spawned = Describe(root);
    const Node made = Describe(expected);
    INFO("The tree under the root should be C++'s: two_parts, Body drawing its first mesh, under it Tip and "
         "Body mesh 2 drawing the second, with the same names, places, meshes and materials, in order");
    REQUIRE(spawned.name == "two_parts");
    REQUIRE(spawned.children.size() == 1);
    REQUIRE(spawned.children[0].children.size() == 2);
    REQUIRE(SameBelow(spawned, made));
}

TEST_CASE("SpawnModel from C# with no parent spawns at the top of the scene", "[renderer][scripting]")
{
    RendererScriptingApp test;
    test.Call<void>(ModelSpawnerType, "Arm", noEntity);
    test.Frame();

    const auto rootId = test.Call<std::uint32_t>(ModelSpawnerType, "Root");
    REQUIRE(test.Entities().HasEntity(rootId));
    INFO("The root should have no parent");
    REQUIRE_FALSE(test.Entities().GetEntity(rootId).GetComponent<Relationship>().parent.IsValid());
}
