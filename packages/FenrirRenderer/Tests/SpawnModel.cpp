// SpawnModel on the null backend (ADR-0023, ADR-0031, ADR-0041): a model's nodes as entities through SpawnNodes, its
// skeletons through the skinning source, then a MeshRenderer per mesh (and a SkinnedMesh per skinned one), and the
// draws the renderer then sees.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirAssets/Model.hpp"
#include "FenrirECS/ComponentType.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirRenderer/SpawnModel.hpp"
#include "FenrirSkeleton/SkeletonPose.hpp"
#include "SkinningTestApp.hpp"

using Fenrir::Entity;
using RendererTest::RendererTestApp;

namespace
{
    Entity ParentOf(const Entity& entity)
    {
        return entity.GetComponent<Fenrir::Relationship>().parent;
    }

    /** @brief Whether `entity` is `ancestor` or below it. */
    bool IsUnder(Entity entity, const Entity& ancestor)
    {
        while (entity.IsValid())
        {
            if (entity == ancestor)
            {
                return true;
            }
            entity = ParentOf(entity);
        }
        return false;
    }

    /** @brief Every entity of the active scene with a MeshRenderer. */
    std::vector<Entity> MeshRenderers(RendererTestApp& test)
    {
        std::vector<Entity> found;
        Fenrir::EntityList& entities = test.ActiveScene().GetEntityList();
        const std::array<Fenrir::ComponentType, 1> types{Fenrir::ComponentType::Native<Fenrir::MeshRenderer>()};
        entities.ForEachWith(types, [&](std::uint32_t id) { found.push_back(entities.GetEntity(id)); });
        return found;
    }
} // namespace

TEST_CASE("SpawnModel on the backpack gives one MeshRenderer per mesh under the root, each drawn",
          "[renderer][spawn-model]")
{
    RendererTestApp test;
    test.AddCamera({0.0f, 0.0f, 3.0f});
    const Fenrir::LoadResult<Fenrir::Model> model =
        test.Store().Load<Fenrir::Model>(FENRIR_TEST_BACKPACK, Fenrir::AssetOwner::App());
    INFO("the backpack loads");
    REQUIRE(model.Ok());
    const Fenrir::Model& data = *test.Store().TryGet(model.handle);
    std::multiset<std::pair<std::uint32_t, std::uint32_t>> expected;
    for (const Fenrir::ModelNode& node : data.nodes)
    {
        for (const Fenrir::ModelMesh& mesh : node.meshes)
        {
            expected.insert({mesh.mesh.index, mesh.material.index});
        }
    }
    INFO("the backpack has meshes to spawn");
    REQUIRE(expected.size() > 1);

    const Entity root = Fenrir::SpawnModel(test.app, test.ActiveScene(), model.handle);
    test.Frame();

    INFO("the root is named after the file");
    REQUIRE(root.IsValid());
    REQUIRE(std::string(root.GetComponent<Fenrir::Name>().Get()) == "backpack");
    const std::vector<Entity> renderers = MeshRenderers(test);
    std::multiset<std::pair<std::uint32_t, std::uint32_t>> spawned;
    for (const Entity& entity : renderers)
    {
        const Fenrir::MeshRenderer& renderer = entity.GetComponent<Fenrir::MeshRenderer>();
        spawned.insert({renderer.mesh.index, renderer.material.index});
        INFO("every MeshRenderer is under the returned root");
        REQUIRE(IsUnder(entity, root));
    }
    INFO("one MeshRenderer per mesh of each node, with its material");
    REQUIRE(spawned == expected);
    INFO("the renderer draws each of them, with builtin:lit, and logs no error");
    const std::vector<RendererTest::RecordedDraw> draws = test.Draws();
    REQUIRE(draws.size() == expected.size());
    for (const RendererTest::RecordedDraw& draw : draws)
    {
        REQUIRE(Fenrir::RendererProbe::Label(test.Device(), draw.pipeline) == "BuiltinLit");
    }
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("SpawnModel puts a node's first mesh on its entity and each further mesh on a child entity",
          "[renderer][spawn-model]")
{
    RendererTestApp test;
    test.AddCamera();
    // One node, Body, whose glTF mesh has two primitives with two materials: two model meshes on one node.
    const Fenrir::LoadResult<Fenrir::Model> model =
        test.Store().Load<Fenrir::Model>(FENRIR_TEST_TWO_PARTS, Fenrir::AssetOwner::App());
    REQUIRE(model.Ok());
    const Fenrir::ModelNode& body = test.Store().TryGet(model.handle)->nodes.at(0);
    INFO("the importer gives Body two meshes");
    REQUIRE(body.meshes.size() == 2);
    Entity holder = test.ActiveScene().GetEntityList().CreateEntity();

    const Entity root = Fenrir::SpawnModel(test.app, test.ActiveScene(), model.handle, holder);
    test.Frame();

    INFO("the root hangs from the given parent");
    REQUIRE(ParentOf(root) == holder);
    const std::vector<Entity> renderers = MeshRenderers(test);
    INFO("two MeshRenderers: Body's two meshes; its child node Tip has none");
    REQUIRE(renderers.size() == 2);
    Entity first;
    Entity second;
    for (const Entity& entity : renderers)
    {
        (entity.GetComponent<Fenrir::MeshRenderer>().mesh == body.meshes[0].mesh ? first : second) = entity;
    }
    INFO("the first mesh, with its material, is on Body's own entity, a child of the root");
    REQUIRE(std::string(first.GetComponent<Fenrir::Name>().Get()) == "Body");
    REQUIRE(ParentOf(first) == root);
    REQUIRE(first.GetComponent<Fenrir::MeshRenderer>().material == body.meshes[0].material);
    INFO("the second is on a child entity of Body's, named for its place, with its own material");
    REQUIRE(ParentOf(second) == first);
    REQUIRE(std::string(second.GetComponent<Fenrir::Name>().Get()) == "Body mesh 2");
    REQUIRE(second.GetComponent<Fenrir::MeshRenderer>().mesh == body.meshes[1].mesh);
    REQUIRE(second.GetComponent<Fenrir::MeshRenderer>().material == body.meshes[1].material);
    INFO("both are drawn");
    REQUIRE(test.Draws().size() == 2);
}

namespace
{
    /** @brief The entities of the active scene with a SkinnedMesh. */
    std::vector<Entity> SkinnedMeshes(RendererTestApp& test)
    {
        std::vector<Entity> found;
        Fenrir::EntityList& entities = test.ActiveScene().GetEntityList();
        const std::array<Fenrir::ComponentType, 1> types{Fenrir::ComponentType::Native<Fenrir::SkinnedMesh>()};
        entities.ForEachWith(types, [&](std::uint32_t id) { found.push_back(entities.GetEntity(id)); });
        return found;
    }

    /** @brief How many of the armatures fixture's meshes are skinned. */
    std::size_t SkinnedCount(const Fenrir::Model& model)
    {
        std::size_t skinned = 0;
        for (const Fenrir::ModelNode& node : model.nodes)
        {
            for (const Fenrir::ModelMesh& mesh : node.meshes)
            {
                skinned += mesh.skeleton >= 0 ? 1 : 0;
            }
        }
        return skinned;
    }
} // namespace

TEST_CASE("SpawnModel gives each skinned mesh an entity under its skeleton's owner, skinned by it, and draws them",
          "[renderer][spawn-model]")
{
    RendererTest::SkinningTestApp test;
    test.AddCamera({0.0f, 0.0f, 10.0f});
    // FenrirAssets's armatures fixture: four skinned meshes on three skeletons, and HatMesh, a rigid mesh on Hat.
    const Fenrir::LoadResult<Fenrir::Model> model =
        test.Store().Load<Fenrir::Model>(FENRIR_TEST_ARMATURES, Fenrir::AssetOwner::App());
    REQUIRE(model.Ok());
    REQUIRE(SkinnedCount(*test.Store().TryGet(model.handle)) == 4);

    const Entity root = Fenrir::SpawnModel(test.app, test.ActiveScene(), model.handle);
    test.Frame();

    REQUIRE(root.IsValid());
    const std::vector<Entity> skinned = SkinnedMeshes(test);
    INFO("one SkinnedMesh per skinned mesh, besides its MeshRenderer");
    REQUIRE(skinned.size() == 4);
    for (const Entity& entity : skinned)
    {
        const Entity owner = entity.GetComponent<Fenrir::SkinnedMesh>().skeleton;
        INFO("each names its skeleton's owner, which has a SkeletonPose, and is its child at the identity");
        REQUIRE(entity.HasComponent<Fenrir::MeshRenderer>());
        REQUIRE(owner.HasComponent<Fenrir::SkeletonPose>());
        REQUIRE(ParentOf(entity) == owner);
        REQUIRE(IsUnder(entity, root));
        const Fenrir::Transform& transform = entity.GetComponent<Fenrir::Transform>();
        REQUIRE(transform.pos == Fenrir::Math::Vec3(0.0f));
        REQUIRE(transform.rot == Fenrir::Math::Quat(1.0f, 0.0f, 0.0f, 0.0f));
        REQUIRE(transform.scale == Fenrir::Math::Vec3(1.0f));
    }
    INFO("the four skinned meshes and the hat are drawn, with no warning about skinning");
    REQUIRE(MeshRenderers(test).size() == 5);
    REQUIRE(test.Draws().size() == 5);
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "skin") == 0);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("SpawnModel without a skinning source spawns skinned meshes with their SkinnedMeshes but no skeletons, "
          "logged once",
          "[renderer][spawn-model]")
{
    RendererTest::SkinningTestApp test(false);
    const Fenrir::LoadResult<Fenrir::Model> model =
        test.Store().Load<Fenrir::Model>(FENRIR_TEST_ARMATURES, Fenrir::AssetOwner::App());
    REQUIRE(model.Ok());

    const Entity root = Fenrir::SpawnModel(test.app, test.ActiveScene(), model.handle);

    REQUIRE(root.IsValid());
    const std::vector<Entity> skinned = SkinnedMeshes(test);
    REQUIRE(skinned.size() == 4);
    for (const Entity& entity : skinned)
    {
        const Entity owner = entity.GetComponent<Fenrir::SkinnedMesh>().skeleton;
        INFO("each names its owner, which has no SkeletonPose: game code may add one later");
        REQUIRE(ParentOf(entity) == owner);
        REQUIRE_FALSE(owner.HasComponent<Fenrir::SkeletonPose>());
    }
    INFO("one warning says the skeletons were not spawned");
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "no skinning source") == 1);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("SpawnModel with a stale model spawns nothing and returns a null entity", "[renderer][spawn-model]")
{
    RendererTestApp test;
    const Fenrir::LoadResult<Fenrir::Model> model =
        test.Store().Load<Fenrir::Model>(FENRIR_TEST_TWO_PARTS, Fenrir::AssetOwner::App());
    REQUIRE(model.Ok());
    test.Store().Unload(model.handle);

    const Entity root = Fenrir::SpawnModel(test.app, test.ActiveScene(), model.handle);

    INFO("the root is null and nothing has a MeshRenderer");
    REQUIRE_FALSE(root.IsValid());
    REQUIRE(MeshRenderers(test).empty());
}
