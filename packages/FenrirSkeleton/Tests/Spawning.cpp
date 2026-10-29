// Spawning (ADR-0031, ADR-0041): SpawnSkeletons and Skeletons::SpawnModel on models built in code, until the glTF
// importer's fixtures land.
#include <catch2/catch_test_macros.hpp>

#include "FenrirAssets/Model.hpp"
#include "SkeletonTest.hpp"

using Fenrir::Entity;
using Fenrir::Skeletons;
using SkeletonTest::Near;
using SkeletonTest::SkeletonApp;
using SkeletonTest::Translation;

namespace
{
    Fenrir::ModelNode Node(std::string name, std::int32_t parent,
                           Fenrir::ModelNodeKind kind = Fenrir::ModelNodeKind::Ordinary)
    {
        Fenrir::ModelNode node;
        node.name = std::move(name);
        node.parent = parent;
        node.kind = kind;
        return node;
    }

    Fenrir::ModelNode BoneNode(std::string name, std::int32_t parent, std::int32_t skeleton, std::int32_t bone)
    {
        Fenrir::ModelNode node = Node(std::move(name), parent, Fenrir::ModelNodeKind::Bone);
        node.skeleton = skeleton;
        node.bone = bone;
        return node;
    }

    // Character (the owner, at (2, 0, 0)) -> root, mid, tip: the chain's bones.
    Fenrir::Model Character(Fenrir::AssetHandle<Fenrir::Skeleton> chain)
    {
        Fenrir::Model model;
        model.nodes = {Node("Character", -1), BoneNode("root", 0, 0, 0), BoneNode("mid", 1, 0, 1),
                       BoneNode("tip", 2, 0, 2)};
        model.nodes[0].rest.pos = {2.0f, 0.0f, 0.0f};
        model.skeletons = {chain};
        model.skeletonOwners = {0};
        return model;
    }
} // namespace

TEST_CASE("Skeletons::SpawnModel gives a skeleton's owner a SkeletonPose, Ready at once", "[skeleton][spawn]")
{
    SkeletonApp test;
    const Fenrir::AssetHandle<Fenrir::Skeleton> chain = SkeletonTest::AddChain(test);
    const Fenrir::AssetHandle<Fenrir::Model> model = test.store.Add<Fenrir::Model>(Character(chain), test.level);

    const Fenrir::SpawnedNodes spawned = test.skeletons.SpawnModel(test.level, model);

    const Entity owner = spawned.nodes[0];
    INFO("the owner is the skeleton entity, posing the model's skeleton, with no frame run");
    REQUIRE(owner.HasComponent<Fenrir::SkeletonPose>());
    REQUIRE(owner.GetComponent<Fenrir::SkeletonPose>().skeleton == chain);
    REQUIRE(Skeletons::GetPoseState(test.level, owner) == Fenrir::PoseState::Ready);

    INFO("its bones are placed from the owner: the tip three units up from (2, 0, 0)");
    REQUIRE(Near(Translation(*Skeletons::GetBoneWorldMatrix(test.level, owner, 2)), {2.0f, 3.0f, 0.0f}));
}

TEST_CASE("SpawnSkeletons gives each synthetic owner its own skeleton", "[skeleton][spawn]")
{
    SkeletonApp test;
    const Fenrir::AssetHandle<Fenrir::Skeleton> left = SkeletonTest::AddChain(test);
    const Fenrir::AssetHandle<Fenrir::Skeleton> right = SkeletonTest::AddChain(test);
    // Two rigs whose owners would both be Pivot, so each has a synthetic owner below it.
    Fenrir::Model model;
    model.nodes = {Node("Pivot", -1), Node("root.Skeleton", 0, Fenrir::ModelNodeKind::SyntheticOwner),
                   Node("root#2.Skeleton", 0, Fenrir::ModelNodeKind::SyntheticOwner), BoneNode("root", 0, 0, 0),
                   BoneNode("root#2", 0, 1, 0)};
    model.skeletons = {left, right};
    model.skeletonOwners = {1, 2};
    const Fenrir::AssetHandle<Fenrir::Model> handle = test.store.Add<Fenrir::Model>(std::move(model), test.level);

    const Fenrir::SpawnedNodes spawned = test.store.SpawnNodes(test.level, handle);
    test.skeletons.SpawnSkeletons(test.level, handle, spawned);

    INFO("each synthetic owner poses its own skeleton, Ready");
    REQUIRE(spawned.nodes[1].GetComponent<Fenrir::SkeletonPose>().skeleton == left);
    REQUIRE(spawned.nodes[2].GetComponent<Fenrir::SkeletonPose>().skeleton == right);
    REQUIRE(Skeletons::GetPoseState(test.level, spawned.nodes[1]) == Fenrir::PoseState::Ready);
    REQUIRE(Skeletons::GetPoseState(test.level, spawned.nodes[2]) == Fenrir::PoseState::Ready);
    INFO("the node they share carries no SkeletonPose");
    REQUIRE(!spawned.nodes[0].HasComponent<Fenrir::SkeletonPose>());
}

TEST_CASE("SpawnSkeletons leaves the owner of a skeleton that did not load without a SkeletonPose", "[skeleton][spawn]")
{
    SkeletonApp test;
    const Fenrir::AssetHandle<Fenrir::Skeleton> chain = SkeletonTest::AddChain(test);
    const Fenrir::AssetHandle<Fenrir::Model> model = test.store.Add<Fenrir::Model>(Character(chain), test.level);
    REQUIRE(test.store.Unload(chain));

    const Fenrir::SpawnedNodes spawned = test.skeletons.SpawnModel(test.level, model);

    INFO("only owners whose skeleton loaded become skeleton entities");
    REQUIRE(!spawned.nodes[0].HasComponent<Fenrir::SkeletonPose>());
}

TEST_CASE("an owner that already has a SkeletonPose keeps it, with a warning", "[skeleton][spawn]")
{
    SkeletonApp test;
    const Fenrir::AssetHandle<Fenrir::Skeleton> chain = SkeletonTest::AddChain(test);
    const Fenrir::AssetHandle<Fenrir::Skeleton> other = SkeletonTest::AddChain(test);
    const Fenrir::AssetHandle<Fenrir::Model> model = test.store.Add<Fenrir::Model>(Character(chain), test.level);
    const Fenrir::SpawnedNodes spawned = test.store.SpawnNodes(test.level, model);
    Entity(spawned.nodes[0]).AddComponent<Fenrir::SkeletonPose>(Fenrir::SkeletonPose{other});

    test.skeletons.SpawnSkeletons(test.level, model, spawned);

    INFO("the existing SkeletonPose is kept");
    REQUIRE(spawned.nodes[0].GetComponent<Fenrir::SkeletonPose>().skeleton == other);
    REQUIRE(test.Warnings("already has a SkeletonPose") == 1);
}

TEST_CASE("the skinning source spawns skeletons as Skeletons::SpawnSkeletons does", "[skeleton][spawn]")
{
    SkeletonApp test;
    const Fenrir::AssetHandle<Fenrir::Model> model =
        test.store.Add<Fenrir::Model>(Character(SkeletonTest::AddChain(test)), test.level);
    const Fenrir::SpawnedNodes spawned = test.store.SpawnNodes(test.level, model);

    // What the renderer's SpawnModel does, knowing only FenrirAssets.
    Fenrir::SkinningSource* source = test.app.GetResource<Fenrir::SkinningSourceRegistration>().source;
    source->SpawnSkeletons(test.level, model, spawned);

    INFO("the owner is a Ready skeleton entity");
    REQUIRE(source->GetPoseState(test.level, spawned.nodes[0]) == Fenrir::PoseState::Ready);
}
