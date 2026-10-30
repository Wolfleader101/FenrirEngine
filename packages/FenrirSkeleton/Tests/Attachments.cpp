// Bone attachments (ADR-0037), seam 8: placement under the skeleton entity and under other parents, dependency order,
// cycles, every AttachmentState with keep-last-transform and recovery, resolution, names, Detach, and spawning. The
// chain skeleton stands at (2, 0, 0) unless a case moves it, so world and model space differ.
#include <cstdint>
#include <cstring>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "FenrirAssets/Model.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "SkeletonTest.hpp"

using Fenrir::AttachmentState;
using Fenrir::BoneAttachment;
using Fenrir::DetachMode;
using Fenrir::DetachResult;
using Fenrir::Entity;
using Fenrir::Skeletons;
using Fenrir::Transform;
using Fenrir::Math::Vec3;
using SkeletonTest::Near;
using SkeletonTest::SkeletonApp;
using SkeletonTest::Translation;

namespace
{
    constexpr std::int32_t Root = 0;
    constexpr std::int32_t Tip = 2;

    // The chain at (2, 0, 0), Ready.
    struct Chain
    {
        SkeletonApp test;
        Fenrir::AssetHandle<Fenrir::Skeleton> asset = SkeletonTest::AddChain(test);
        Entity skeleton = SkeletonTest::SkeletonEntity(test.level, asset);

        Chain()
        {
            skeleton.GetComponent<Transform>().pos = {2.0f, 0.0f, 0.0f};
            test.skeletons.Ensure(test.level, skeleton);
        }

        Entity Make()
        {
            return test.level.GetEntityList().CreateEntity();
        }

        // `entity` attached to `bone` of `on`, at `offset` from it.
        void Attach(Entity entity, Entity on, const std::string& bone, Vec3 offset = Vec3(0.0f))
        {
            BoneAttachment attachment;
            attachment.skeleton = on;
            REQUIRE(attachment.bone.Set(bone, *test.app.Logger()));
            attachment.offset.pos = offset;
            entity.AddComponent<BoneAttachment>(attachment);
        }

        [[nodiscard]] AttachmentState State(Entity entity)
        {
            return Skeletons::GetAttachmentState(test.level, entity);
        }

        // Turns the root a quarter about z, so the tip swings from (0, 3, 0) to (-2, 1, 0) in model space.
        void Bend()
        {
            Skeletons::EditBasePose(test.level, skeleton)[Root].rot = SkeletonTest::QuarterTurnZ();
        }
    };

    Vec3 WorldPos(const Entity& entity)
    {
        return Translation(Fenrir::GetWorldMatrix(entity));
    }

    // Where `bone` of `skeleton` puts a point `offset` from it, read the one defined way.
    Vec3 AtBone(Fenrir::Scene& scene, Entity skeleton, std::int32_t bone, Vec3 offset)
    {
        return Translation(*Skeletons::GetBoneWorldMatrix(scene, skeleton, bone) *
                           Fenrir::Math::Translate(Fenrir::Math::Mat4(1.0f), offset));
    }
} // namespace

TEST_CASE("an attachment under its skeleton entity follows its bone", "[skeleton][attach]")
{
    Chain chain;
    Entity prop = chain.Make();
    chain.skeleton.AddChild(prop);
    chain.Attach(prop, chain.skeleton, "tip", {1.0f, 0.0f, 0.0f});
    INFO("before any Attach the state is NotAttached");
    REQUIRE(chain.State(prop) == AttachmentState::NotAttached);

    chain.test.RunFrame();
    INFO("its local Transform is M_model[tip] × offset, and its world place the bone's world matrix × offset");
    REQUIRE(chain.State(prop) == AttachmentState::Attached);
    REQUIRE(Near(prop.GetComponent<Transform>().pos, {1.0f, 3.0f, 0.0f}));
    REQUIRE(Near(WorldPos(prop), {3.0f, 3.0f, 0.0f}));

    chain.Bend();
    chain.test.RunFrame();
    INFO("the bent chain carries it round, the bone's rotation included");
    REQUIRE(Near(WorldPos(prop), AtBone(chain.test.level, chain.skeleton, Tip, {1.0f, 0.0f, 0.0f})));
    REQUIRE(Near(WorldPos(prop), {0.0f, 2.0f, 0.0f}));
    REQUIRE(Near(prop.GetComponent<Transform>().rot, SkeletonTest::QuarterTurnZ()));

    INFO("a direct write to the Transform is overwritten at the next Attach");
    prop.GetComponent<Transform>().pos = {50.0f, 0.0f, 0.0f};
    chain.test.RunFrame();
    REQUIRE(Near(WorldPos(prop), {0.0f, 2.0f, 0.0f}));
}

TEST_CASE("an attachment under another parent is placed through that parent's inverse", "[skeleton][attach]")
{
    Chain chain;
    Entity holder = chain.Make();
    Transform& at = holder.GetComponent<Transform>();
    at.pos = {-4.0f, 1.0f, 5.0f};
    at.rot = Fenrir::Math::AngleAxis(0.7f, Vec3(0.0f, 1.0f, 0.0f));
    at.scale = Vec3(2.0f);
    Entity prop = chain.Make();
    holder.AddChild(prop);
    chain.Attach(prop, chain.skeleton, "mid", {0.0f, 0.5f, 0.0f});
    chain.Bend();

    chain.test.RunFrame();
    INFO("its world place is the bone's, whatever its parent");
    REQUIRE(chain.State(prop) == AttachmentState::Attached);
    REQUIRE(Near(WorldPos(prop), AtBone(chain.test.level, chain.skeleton, 1, {0.0f, 0.5f, 0.0f})));
    INFO("the parent's scale is undone in the local Transform");
    REQUIRE(Near(prop.GetComponent<Transform>().scale, Vec3(0.5f)));

    INFO("a child of the attached entity is carried with it at no cost");
    Entity child = chain.Make();
    child.GetComponent<Transform>().pos = {0.0f, 0.0f, 1.0f};
    prop.AddChild(child);
    chain.test.RunFrame();
    REQUIRE(Near(WorldPos(child), AtBone(chain.test.level, chain.skeleton, 1, {0.0f, 0.5f, 1.0f})));
}

TEST_CASE("attachments are placed in dependency order, whatever their entity order", "[skeleton][attach]")
{
    Chain chain;
    // Made first, so the lowest index: the scope on the rifle, then a skeleton under an attached hand.
    Entity scope = chain.Make();
    Entity onHand = chain.Make();
    Entity rifle = SkeletonTest::SkeletonEntity(chain.test.level, SkeletonTest::AddChain(chain.test));
    chain.test.skeletons.Ensure(chain.test.level, rifle);
    Entity hand = chain.Make();
    Entity glove = SkeletonTest::SkeletonEntity(chain.test.level, SkeletonTest::AddChain(chain.test));
    chain.test.skeletons.Ensure(chain.test.level, glove);
    hand.AddChild(glove);

    // An attachment on an attachment: the rifle on the chain's tip, the scope on the rifle's mid.
    chain.Attach(rifle, chain.skeleton, "tip", {0.5f, 0.0f, 0.0f});
    chain.Attach(scope, rifle, "mid", {0.0f, 0.2f, 0.0f});
    // A skeleton under an attached entity: the glove under the hand on the chain's mid, onHand on the glove's root.
    chain.Attach(hand, chain.skeleton, "mid");
    chain.Attach(onHand, glove, "root");

    chain.Bend();
    chain.test.RunFrame();
    INFO("each is where this frame's pose puts it, after the attachments it depends on, in one pass");
    for (const Entity& entity : {scope, onHand, rifle, hand})
    {
        REQUIRE(chain.State(entity) == AttachmentState::Attached);
    }
    REQUIRE(Near(WorldPos(rifle), AtBone(chain.test.level, chain.skeleton, Tip, {0.5f, 0.0f, 0.0f})));
    REQUIRE(Near(WorldPos(scope), AtBone(chain.test.level, rifle, 1, {0.0f, 0.2f, 0.0f})));
    REQUIRE(Near(WorldPos(onHand), AtBone(chain.test.level, glove, Root, {0.0f, 0.0f, 0.0f})));
}

TEST_CASE("attachments in a cycle are Unplaceable, logged once, and keep their Transforms", "[skeleton][attach]")
{
    Chain chain;
    // A sword attached to a bone of a skeleton parented under the sword.
    Entity sword = chain.Make();
    sword.GetComponent<Transform>().pos = {7.0f, 0.0f, 0.0f};
    sword.AddChild(chain.skeleton);
    chain.Attach(sword, chain.skeleton, "tip");
    // Something placed on the sword's skeleton still depends on the cycle, and is placed.
    Entity follower = chain.Make();
    chain.Attach(follower, chain.skeleton, "root");

    chain.test.RunFrame();
    chain.test.RunFrame();
    REQUIRE(chain.State(sword) == AttachmentState::Unplaceable);
    REQUIRE(Near(sword.GetComponent<Transform>().pos, {7.0f, 0.0f, 0.0f}));
    REQUIRE(chain.test.Warnings("dependency cycle") == 1);
    REQUIRE(chain.State(follower) == AttachmentState::Attached);

    INFO("breaking the cycle recovers it");
    sword.RemoveChild(chain.skeleton);
    chain.test.RunFrame();
    REQUIRE(chain.State(sword) == AttachmentState::Attached);
}

TEST_CASE("every AttachmentState, each failure keeping the last Transform and recovering", "[skeleton][attach]")
{
    Chain chain;
    Entity prop = chain.Make();
    chain.Attach(prop, chain.skeleton, "tip");
    chain.test.RunFrame();
    REQUIRE(chain.State(prop) == AttachmentState::Attached);
    const Vec3 placed = prop.GetComponent<Transform>().pos;
    const auto kept = [&] { return Near(prop.GetComponent<Transform>().pos, placed); };

    SECTION("SkeletonMissing: the skeleton entity loses its SkeletonPose, then gets it back")
    {
        chain.skeleton.RemoveComponent<Fenrir::SkeletonPose>();
        chain.test.RunFrame();
        chain.test.RunFrame();
        REQUIRE(chain.State(prop) == AttachmentState::SkeletonMissing);
        REQUIRE(kept());
        REQUIRE(chain.test.Warnings("SkeletonMissing") == 1);
        chain.skeleton.AddComponent<Fenrir::SkeletonPose>(Fenrir::SkeletonPose{chain.asset});
        chain.test.RunFrame();
        REQUIRE(chain.State(prop) == AttachmentState::Attached);
    }
    SECTION("SkeletonMissing: the skeleton entity is destroyed")
    {
        chain.test.level.GetEntityList().DestroyEntity(chain.skeleton);
        chain.test.RunFrame();
        REQUIRE(chain.State(prop) == AttachmentState::SkeletonMissing);
        REQUIRE(kept());
    }
    SECTION("SkeletonUnavailable: an empty handle, then the asset again")
    {
        chain.skeleton.GetComponent<Fenrir::SkeletonPose>().skeleton = {};
        chain.test.RunFrame();
        REQUIRE(chain.State(prop) == AttachmentState::SkeletonUnavailable);
        REQUIRE(kept());
        chain.skeleton.GetComponent<Fenrir::SkeletonPose>().skeleton = chain.asset;
        chain.test.RunFrame();
        REQUIRE(chain.State(prop) == AttachmentState::Attached);
    }
    SECTION("BoneNotFound: a name no bone has, then a name one has")
    {
        REQUIRE(prop.GetComponent<BoneAttachment>().bone.Set("thumb", *chain.test.app.Logger()));
        chain.test.RunFrame();
        chain.test.RunFrame();
        REQUIRE(chain.State(prop) == AttachmentState::BoneNotFound);
        REQUIRE(kept());
        REQUIRE(chain.test.Warnings("BoneNotFound") == 1);
        REQUIRE(prop.GetComponent<BoneAttachment>().bone.Set("root", *chain.test.app.Logger()));
        chain.test.RunFrame();
        REQUIRE(chain.State(prop) == AttachmentState::Attached);
        REQUIRE(Near(WorldPos(prop), {2.0f, 1.0f, 0.0f}));
    }
    SECTION("TransformMissing: the entity loses its Transform, which Attach never adds back")
    {
        prop.RemoveComponent<Transform>();
        chain.test.RunFrame();
        REQUIRE(chain.State(prop) == AttachmentState::TransformMissing);
        REQUIRE(!prop.HasComponent<Transform>());
        prop.AddComponent<Transform>();
        chain.test.RunFrame();
        REQUIRE(chain.State(prop) == AttachmentState::Attached);
    }
    SECTION("Unplaceable: a parent scaled to nothing, then scaled back")
    {
        Entity flat = chain.Make();
        flat.GetComponent<Transform>().scale = {1.0f, 0.0f, 1.0f};
        flat.AddChild(prop);
        prop.GetComponent<Transform>().pos = placed;
        chain.test.RunFrame();
        REQUIRE(chain.State(prop) == AttachmentState::Unplaceable);
        REQUIRE(kept());
        REQUIRE(chain.test.Warnings("no inverse") == 1);
        flat.GetComponent<Transform>().scale = Vec3(1.0f);
        chain.test.RunFrame();
        REQUIRE(chain.State(prop) == AttachmentState::Attached);
    }
    SECTION("NotAttached: the component removed")
    {
        prop.RemoveComponent<BoneAttachment>();
        chain.test.RunFrame();
        REQUIRE(chain.State(prop) == AttachmentState::NotAttached);
    }
}

TEST_CASE("a rebuilt skeleton re-resolves its attachments by name", "[skeleton][attach]")
{
    Chain chain;
    Entity prop = chain.Make();
    chain.Attach(prop, chain.skeleton, "tip");
    chain.test.RunFrame();
    REQUIRE(Near(WorldPos(prop), {2.0f, 3.0f, 0.0f}));

    SECTION("bones reordered: the same name, another index")
    {
        Fenrir::SkeletonData reordered;
        reordered.bones = {SkeletonTest::Bone("root", -1, {0.0f, 1.0f, 0.0f}),
                           SkeletonTest::Bone("tip", 0, {0.0f, 5.0f, 0.0f})};
        REQUIRE(chain.test.store.Replace(chain.asset, std::move(reordered)));
        chain.test.RunFrame();
        REQUIRE(chain.State(prop) == AttachmentState::Attached);
        REQUIRE(Near(WorldPos(prop), {2.0f, 6.0f, 0.0f}));
    }
    SECTION("the bone gone, then back in another rebuild: BoneNotFound until it resolves")
    {
        Fenrir::SkeletonData stub;
        stub.bones = {SkeletonTest::Bone("root", -1, {0.0f, 1.0f, 0.0f})};
        REQUIRE(chain.test.store.Replace(chain.asset, std::move(stub)));
        chain.test.RunFrame();
        REQUIRE(chain.State(prop) == AttachmentState::BoneNotFound);
        REQUIRE(Near(WorldPos(prop), {2.0f, 3.0f, 0.0f}));
        REQUIRE(chain.test.store.Replace(chain.asset, SkeletonTest::ChainData()));
        chain.test.RunFrame();
        REQUIRE(chain.State(prop) == AttachmentState::Attached);
    }
    SECTION("another skeleton entity named: resolved against it")
    {
        Entity other = SkeletonTest::SkeletonEntity(chain.test.level, chain.asset);
        other.GetComponent<Transform>().pos = {-5.0f, 0.0f, 0.0f};
        prop.GetComponent<BoneAttachment>().skeleton = other;
        chain.test.RunFrame();
        REQUIRE(Near(WorldPos(prop), {-5.0f, 3.0f, 0.0f}));
    }
}

TEST_CASE("BoneName refuses over-long names and never resolves an unterminated one", "[skeleton][attach]")
{
    Chain chain;
    Entity prop = chain.Make();
    chain.Attach(prop, chain.skeleton, "tip");

    INFO("255 bytes is the limit: 256 is refused, logged, and the name unchanged, never truncated");
    Fenrir::BoneName& name = prop.GetComponent<BoneAttachment>().bone;
    REQUIRE(name.Set(std::string(255, 'a'), *chain.test.app.Logger()));
    REQUIRE(name.Get()->size() == 255);
    REQUIRE(name.Set("tip", *chain.test.app.Logger()));
    REQUIRE(!name.Set(std::string(256, 'a'), *chain.test.app.Logger()));
    REQUIRE(chain.test.Errors("over the 255-byte limit") == 1);
    REQUIRE(name.Get() == "tip");

    chain.test.RunFrame();
    REQUIRE(chain.State(prop) == AttachmentState::Attached);
    const Vec3 placed = prop.GetComponent<Transform>().pos;

    INFO("a buffer written raw with no terminator is malformed: diagnosed, never resolved as a partial name");
    std::memset(static_cast<void*>(&name), 't', sizeof(name));
    REQUIRE(!name.IsTerminated());
    REQUIRE(!name.Get().has_value());
    chain.test.RunFrame();
    REQUIRE(chain.State(prop) == AttachmentState::BoneNotFound);
    REQUIRE(chain.test.Warnings("no terminator") == 1);
    REQUIRE(Near(prop.GetComponent<Transform>().pos, placed));
}

TEST_CASE("Detach keeps the local or the world transform, and KeepWorld changes nothing on failure",
          "[skeleton][attach]")
{
    Chain chain;
    Entity prop = chain.Make();
    chain.skeleton.AddChild(prop);
    chain.Attach(prop, chain.skeleton, "tip", {1.0f, 0.0f, 0.0f});
    chain.Bend();
    chain.test.RunFrame();
    const Vec3 world = WorldPos(prop);
    const Transform local = prop.GetComponent<Transform>();

    SECTION("KeepLocal removes the component only: the entity follows its parent, not the bone")
    {
        REQUIRE(Skeletons::Detach(chain.test.level, prop, DetachMode::KeepLocal) == DetachResult::Detached);
        REQUIRE(!prop.HasComponent<BoneAttachment>());
        REQUIRE(prop.GetComponent<Fenrir::Relationship>().parent == chain.skeleton);
        Skeletons::EditBasePose(chain.test.level, chain.skeleton)[Root].rot = Fenrir::Math::Quat(1, 0, 0, 0);
        chain.test.RunFrame();
        REQUIRE(Near(prop.GetComponent<Transform>().pos, local.pos));
        REQUIRE(chain.State(prop) == AttachmentState::NotAttached);
    }
    SECTION("KeepWorld under a new parent, and to the top of the scene")
    {
        Entity table = chain.Make();
        Transform& at = table.GetComponent<Transform>();
        at.pos = {0.0f, 0.0f, -3.0f};
        at.rot = Fenrir::Math::AngleAxis(1.1f, Vec3(0.0f, 1.0f, 0.0f));
        at.scale = Vec3(1.5f);
        REQUIRE(Skeletons::Detach(chain.test.level, prop, DetachMode::KeepWorld, table) == DetachResult::Detached);
        REQUIRE(prop.GetComponent<Fenrir::Relationship>().parent == table);
        REQUIRE(Near(WorldPos(prop), world));
        // The skeleton moving no longer moves it.
        chain.skeleton.GetComponent<Transform>().pos = {9.0f, 0.0f, 0.0f};
        chain.test.RunFrame();
        REQUIRE(Near(WorldPos(prop), world));

        Entity other = chain.Make();
        chain.Attach(other, chain.skeleton, "mid");
        chain.test.RunFrame();
        const Vec3 otherWorld = WorldPos(other);
        REQUIRE(Skeletons::Detach(chain.test.level, other, DetachMode::KeepWorld) == DetachResult::Detached);
        REQUIRE(!other.GetComponent<Fenrir::Relationship>().parent.IsValid());
        REQUIRE(Near(WorldPos(other), otherWorld));
    }
    SECTION("KeepWorld refusals change nothing, the parent and component included")
    {
        Entity child = chain.Make();
        prop.AddChild(child);
        Entity flat = chain.Make();
        flat.GetComponent<Transform>().scale = {0.0f, 1.0f, 1.0f};
        Entity dead = chain.Make();
        chain.test.level.GetEntityList().DestroyEntity(dead);

        REQUIRE(Skeletons::Detach(chain.test.level, prop, DetachMode::KeepWorld, prop) == DetachResult::InvalidParent);
        REQUIRE(Skeletons::Detach(chain.test.level, prop, DetachMode::KeepWorld, child) == DetachResult::InvalidParent);
        REQUIRE(Skeletons::Detach(chain.test.level, prop, DetachMode::KeepWorld, dead) == DetachResult::InvalidParent);
        REQUIRE(Skeletons::Detach(chain.test.level, prop, DetachMode::KeepWorld, flat) ==
                DetachResult::ParentNotInvertible);
        REQUIRE(prop.HasComponent<BoneAttachment>());
        REQUIRE(prop.GetComponent<Fenrir::Relationship>().parent == chain.skeleton);
        REQUIRE(Near(prop.GetComponent<Transform>().pos, local.pos));
        REQUIRE(Skeletons::Detach(chain.test.level, chain.Make(), DetachMode::KeepLocal) == DetachResult::NotAttached);
    }
    SECTION("refused inside the pose pipeline")
    {
        DetachResult inside = DetachResult::Detached;
        const Fenrir::PoseStageHandle late =
            chain.test.skeletons.AddLate("detach", 0, [&](Fenrir::LateContext& context) {
                inside = Skeletons::Detach(context.GetScene(), prop, DetachMode::KeepLocal);
            });
        chain.test.RunFrame();
        REQUIRE(inside == DetachResult::InsidePipeline);
        REQUIRE(prop.HasComponent<BoneAttachment>());
        REQUIRE(chain.test.Errors("Skeletons::Detach") == 1);
        REQUIRE(chain.test.skeletons.Remove(late));
    }
}

TEST_CASE("SpawnSkeletons attaches each ordinary node below a bone to it, at its rest place",
          "[skeleton][attach][spawn]")
{
    SkeletonApp test;
    const Fenrir::AssetHandle<Fenrir::Skeleton> asset = SkeletonTest::AddChain(test);
    // Character (the owner, at (2, 0, 0)) -> root, mid, tip; Grip below tip, Gem below Grip, Badge below root.
    Fenrir::Model model;
    const auto node = [](std::string name, std::int32_t parent, Fenrir::ModelNodeKind kind, std::int32_t bone) {
        Fenrir::ModelNode made;
        made.name = std::move(name);
        made.parent = parent;
        made.kind = kind;
        made.skeleton = bone >= 0 ? 0 : -1;
        made.bone = bone;
        return made;
    };
    using Kind = Fenrir::ModelNodeKind;
    model.nodes = {node("Character", -1, Kind::Ordinary, -1),
                   node("root", 0, Kind::Bone, 0),
                   node("mid", 1, Kind::Bone, 1),
                   node("tip", 2, Kind::Bone, 2),
                   node("Grip", 3, Kind::Ordinary, -1),
                   node("Gem", 4, Kind::Ordinary, -1),
                   node("Badge", 1, Kind::Ordinary, -1)};
    model.nodes[0].rest.pos = {2.0f, 0.0f, 0.0f};
    for (std::size_t bone = 1; bone <= 3; ++bone)
    {
        model.nodes[bone].rest.pos = {0.0f, 1.0f, 0.0f};
    }
    model.nodes[4].rest.pos = {0.5f, 0.0f, 0.0f};
    model.nodes[4].rest.rot = SkeletonTest::QuarterTurnZ();
    model.nodes[5].rest.pos = {0.0f, 0.25f, 0.0f};
    model.nodes[6].rest.pos = {0.0f, 0.0f, 0.3f};
    model.skeletons = {asset};
    model.skeletonOwners = {0};
    const auto handle = test.store.Add<Fenrir::Model>(std::move(model), test.level);

    const Fenrir::SpawnedNodes spawned = test.skeletons.SpawnModel(test.level, handle);
    const Entity grip = spawned.nodes[4];
    const Entity gem = spawned.nodes[5];
    const Entity badge = spawned.nodes[6];

    INFO("each node whose parent is a bone is attached to it by engine name, its rest transform the offset");
    REQUIRE(grip.HasComponent<BoneAttachment>());
    REQUIRE(grip.GetComponent<BoneAttachment>().skeleton == spawned.nodes[0]);
    REQUIRE(grip.GetComponent<BoneAttachment>().bone.Get() == "tip");
    REQUIRE(Near(grip.GetComponent<BoneAttachment>().offset.pos, {0.5f, 0.0f, 0.0f}));
    REQUIRE(badge.GetComponent<BoneAttachment>().bone.Get() == "root");
    INFO("a node below an attached node follows it as its child, with no attachment of its own");
    REQUIRE(!gem.HasComponent<BoneAttachment>());

    test.RunFrame();
    INFO("placed at rest, each node's world transform is its rest world transform");
    REQUIRE(Skeletons::GetAttachmentState(test.level, grip) == AttachmentState::Attached);
    REQUIRE(Near(WorldPos(grip), {2.5f, 3.0f, 0.0f}));
    REQUIRE(Near(Fenrir::GetWorldTransform(grip).rot, SkeletonTest::QuarterTurnZ()));
    REQUIRE(Near(WorldPos(gem), {2.25f, 3.0f, 0.0f}));
    REQUIRE(Near(WorldPos(badge), {2.0f, 1.0f, 0.3f}));

    INFO("and it follows its bone");
    Skeletons::EditBasePose(test.level, spawned.nodes[0])[Root].rot = SkeletonTest::QuarterTurnZ();
    test.RunFrame();
    REQUIRE(Near(WorldPos(grip), AtBone(test.level, spawned.nodes[0], Tip, {0.5f, 0.0f, 0.0f})));
}
