// Add<Model>: a model built in code, for tests and procedural content, checked as an imported one would be.
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "AssetsTest.hpp"
#include "FenrirAssets/Model.hpp"
#include "FenrirAssets/SkeletonData.hpp"

using AssetsTest::AssetsApp;
using Fenrir::AssetOwner;

namespace
{
    Fenrir::ModelNode Node(std::string name, std::int32_t parent)
    {
        Fenrir::ModelNode node;
        node.name = std::move(name);
        node.parent = parent;
        return node;
    }

    Fenrir::ModelNode BoneNode(std::string name, std::int32_t parent, std::int32_t skeleton, std::int32_t bone)
    {
        Fenrir::ModelNode node = Node(std::move(name), parent);
        node.kind = Fenrir::ModelNodeKind::Bone;
        node.skeleton = skeleton;
        node.bone = bone;
        return node;
    }

    // Character (the owner) -> hip (bone 0) -> knee (bone 1).
    Fenrir::Model Character(Fenrir::AssetHandle<Fenrir::Skeleton> skeleton)
    {
        Fenrir::Model model;
        model.nodes = {Node("Character", -1), BoneNode("hip", 0, 0, 0), BoneNode("knee", 1, 0, 1)};
        model.skeletons = {skeleton};
        model.skeletonOwners = {0};
        return model;
    }

    Fenrir::AssetHandle<Fenrir::Skeleton> TwoBones(Fenrir::AssetStore& store, AssetOwner owner)
    {
        Fenrir::SkeletonData skeleton;
        skeleton.bones.resize(2);
        skeleton.bones[0].sourceName = "hip";
        skeleton.bones[1].sourceName = "knee";
        skeleton.bones[1].parent = 0;
        return store.Add<Fenrir::Skeleton>(std::move(skeleton), owner);
    }
} // namespace

TEST_CASE("Add<Model> takes a model built in code, with its skeletons and their owners", "[assets][model]")
{
    AssetsApp test;
    Fenrir::Scene& level = test.app.CreateScene("Level");
    const Fenrir::AssetHandle<Fenrir::Skeleton> skeleton = TwoBones(test.store, level);

    const Fenrir::AssetHandle<Fenrir::Model> model = test.store.Add<Fenrir::Model>(Character(skeleton), level);

    INFO("the model is stored as given");
    REQUIRE(test.store.IsValid(model));
    const Fenrir::Model* stored = test.store.TryGet(model);
    REQUIRE(stored->nodes.size() == 3);
    REQUIRE(stored->skeletonOwners.size() == 1);
    REQUIRE(stored->skeletonOwners[0] == 0);
    REQUIRE(test.errors->empty());
}

TEST_CASE("Add<Model> refuses a node before its parent", "[assets][model]")
{
    AssetsApp test;
    Fenrir::Scene& level = test.app.CreateScene("Level");
    Fenrir::Model model;
    model.nodes = {Node("child", 1), Node("parent", -1)};

    INFO("a parent index at or after its child is refused with an error");
    REQUIRE(test.store.Add<Fenrir::Model>(std::move(model), level).IsEmpty());
    REQUIRE(test.errors->size() == 1);
}

TEST_CASE("Add<Model> refuses a bone node naming no skeleton of the model", "[assets][model]")
{
    AssetsApp test;
    Fenrir::Scene& level = test.app.CreateScene("Level");
    Fenrir::Model model = Character(TwoBones(test.store, level));
    model.nodes[2].skeleton = 1;

    INFO("a bone node's skeleton must be one of Model::skeletons");
    REQUIRE(test.store.Add<Fenrir::Model>(std::move(model), level).IsEmpty());
    REQUIRE(test.errors->size() == 1);
}

TEST_CASE("Add<Model> refuses skeleton owners that do not match its skeletons", "[assets][model]")
{
    AssetsApp test;
    Fenrir::Scene& level = test.app.CreateScene("Level");

    Fenrir::Model missing = Character(TwoBones(test.store, level));
    missing.skeletonOwners.clear();
    INFO("every skeleton needs an owner");
    REQUIRE(test.store.Add<Fenrir::Model>(std::move(missing), level).IsEmpty());

    Fenrir::Model onBone = Character(TwoBones(test.store, level));
    onBone.skeletonOwners = {1};
    INFO("an owner is an ordinary node or a synthetic owner, never a bone");
    REQUIRE(test.store.Add<Fenrir::Model>(std::move(onBone), level).IsEmpty());
    REQUIRE(test.errors->size() == 2);
}
