// Skeleton instances and their reads (ADR-0028, ADR-0031): a procedural chain posed by hand, read in local, model and
// world space, and its instance made, rebuilt and swept by reconcile.
#include <catch2/catch_test_macros.hpp>

#include "SkeletonTest.hpp"

using Fenrir::Entity;
using Fenrir::Skeletons;
using SkeletonTest::Near;
using SkeletonTest::SkeletonApp;
using SkeletonTest::Translation;

TEST_CASE("a hand-posed three-bone chain gives its model matrices and its bones' world matrices under a moved "
          "skeleton entity",
          "[skeleton][instances]")
{
    SkeletonApp test;
    const Entity skeleton = SkeletonTest::SkeletonEntity(test.level, SkeletonTest::AddChain(test));
    test.RunFrame();

    // Worked by hand: root at (0, 1, 0) and mid one up at (0, 2, 0). Mid turns a quarter about +Z, so tip's (0, 1, 0)
    // becomes (-1, 0, 0) from mid: (-1, 2, 0) in model space.
    std::span<Fenrir::Transform> base = Skeletons::EditBasePose(test.level, skeleton);
    REQUIRE(base.size() == 3);
    base[1].rot = SkeletonTest::QuarterTurnZ();
    test.RunFrame();

    INFO("the tip's model matrix composes its parents' locals");
    const std::optional<Fenrir::Math::Mat4> tip = Skeletons::GetModel(test.level, skeleton, 2);
    REQUIRE(tip.has_value());
    REQUIRE(Near(Translation(*tip), {-1.0f, 2.0f, 0.0f}));

    INFO("the final pose's local is the base pose's, with no modifier");
    REQUIRE(Near(Skeletons::GetLocal(test.level, skeleton, 1)->rot, SkeletonTest::QuarterTurnZ()));

    // A quarter turn of the skeleton entity about +Z takes (-1, 2, 0) to (-2, -1, 0), and the move adds (10, 0, 0).
    Fenrir::Transform& moved = skeleton.GetComponent<Fenrir::Transform>();
    moved.pos = {10.0f, 0.0f, 0.0f};
    moved.rot = SkeletonTest::QuarterTurnZ();

    INFO("a bone's world matrix is the skeleton entity's world matrix times its model matrix, at once");
    const std::optional<Fenrir::Math::Mat4> world = Skeletons::GetBoneWorldMatrix(test.level, skeleton, 2);
    REQUIRE(world.has_value());
    REQUIRE(Near(Translation(*world), {8.0f, -1.0f, 0.0f}));

    INFO("moving the skeleton entity changes no model matrix");
    test.RunFrame();
    REQUIRE(Near(Translation(*Skeletons::GetModel(test.level, skeleton, 2)), {-1.0f, 2.0f, 0.0f}));
}

TEST_CASE("reconcile makes an instance for a new SkeletonPose at the next pose pipeline, at the rest pose",
          "[skeleton][instances]")
{
    SkeletonApp test;
    const Entity skeleton = SkeletonTest::SkeletonEntity(test.level, SkeletonTest::AddChain(test));

    INFO("before the pipeline has run there is no instance");
    REQUIRE(Skeletons::GetPoseState(test.level, skeleton) == Fenrir::PoseState::Missing);
    REQUIRE(Skeletons::GetLocals(test.level, skeleton).empty());

    test.RunFrame();
    INFO("after it, the instance is Ready at the rest pose: the tip three units up");
    REQUIRE(Skeletons::GetPoseState(test.level, skeleton) == Fenrir::PoseState::Ready);
    REQUIRE(Near(Skeletons::GetLocals(test.level, skeleton)[2].pos, {0.0f, 1.0f, 0.0f}));
    REQUIRE(Near(Translation(*Skeletons::GetModel(test.level, skeleton, 2)), {0.0f, 3.0f, 0.0f}));
}

TEST_CASE("Ensure makes an entity's instance at once, with no frame run", "[skeleton][instances]")
{
    SkeletonApp test;
    const Entity skeleton = SkeletonTest::SkeletonEntity(test.level, SkeletonTest::AddChain(test));

    INFO("Ensure reports the state it reconciled to, and reads work straight away");
    REQUIRE(test.skeletons.Ensure(test.level, skeleton) == Fenrir::PoseState::Ready);
    REQUIRE(Skeletons::GetPoseState(test.level, skeleton) == Fenrir::PoseState::Ready);
    REQUIRE(Skeletons::GetModelMatrices(test.level, skeleton).size() == 3);
}

TEST_CASE("a change of skeleton handle rebuilds the instance at the new skeleton's rest pose", "[skeleton][instances]")
{
    SkeletonApp test;
    Entity skeleton = SkeletonTest::SkeletonEntity(test.level, SkeletonTest::AddChain(test));
    test.RunFrame();
    Skeletons::EditBasePose(test.level, skeleton)[0].pos = {5.0f, 0.0f, 0.0f};
    test.RunFrame();

    Fenrir::SkeletonData two;
    two.bones = {SkeletonTest::Bone("hip", -1, {0.0f, 2.0f, 0.0f}), SkeletonTest::Bone("knee", 0, {0.0f, -1.0f, 0.0f})};
    skeleton.GetComponent<Fenrir::SkeletonPose>().skeleton = test.store.Add<Fenrir::Skeleton>(two, test.level);
    test.RunFrame();

    INFO("the instance has the new skeleton's bones, at its rest pose; the hand pose is gone");
    REQUIRE(Skeletons::GetLocals(test.level, skeleton).size() == 2);
    REQUIRE(Near(Skeletons::GetLocals(test.level, skeleton)[0].pos, {0.0f, 2.0f, 0.0f}));
    REQUIRE(Skeletons::FindBone(test.level, skeleton, "knee") == 1);
    REQUIRE(Skeletons::FindBone(test.level, skeleton, "tip") == Fenrir::NoBone);
}

TEST_CASE("a new revision of the skeleton asset rebuilds the instance at its rest pose", "[skeleton][instances]")
{
    SkeletonApp test;
    const Fenrir::AssetHandle<Fenrir::Skeleton> chain = SkeletonTest::AddChain(test);
    const Entity skeleton = SkeletonTest::SkeletonEntity(test.level, chain);
    test.RunFrame();
    Skeletons::EditBasePose(test.level, skeleton)[0].pos = {5.0f, 0.0f, 0.0f};
    test.RunFrame();

    Fenrir::SkeletonData longer = SkeletonTest::ChainData();
    longer.bones.push_back(SkeletonTest::Bone("end", 2, {0.0f, 1.0f, 0.0f}));
    REQUIRE(test.store.Replace(chain, longer));
    test.RunFrame();

    INFO("the same handle at a new revision gives the replaced bones, at rest");
    REQUIRE(Skeletons::GetLocals(test.level, skeleton).size() == 4);
    REQUIRE(Near(Skeletons::GetLocals(test.level, skeleton)[0].pos, {0.0f, 1.0f, 0.0f}));
    REQUIRE(Near(Translation(*Skeletons::GetModel(test.level, skeleton, 3)), {0.0f, 4.0f, 0.0f}));
}

TEST_CASE("reconcile drops the instance of a removed SkeletonPose or a destroyed entity", "[skeleton][instances]")
{
    SkeletonApp test;
    const Fenrir::AssetHandle<Fenrir::Skeleton> chain = SkeletonTest::AddChain(test);
    Entity removed = SkeletonTest::SkeletonEntity(test.level, chain);
    const Entity destroyed = SkeletonTest::SkeletonEntity(test.level, chain);
    test.RunFrame();

    removed.RemoveComponent<Fenrir::SkeletonPose>();
    test.level.GetEntityList().DestroyEntity(destroyed);
    test.RunFrame();

    INFO("both are Missing, with empty reads");
    REQUIRE(Skeletons::GetPoseState(test.level, removed) == Fenrir::PoseState::Missing);
    REQUIRE(Skeletons::GetPoseState(test.level, destroyed) == Fenrir::PoseState::Missing);
    REQUIRE(Skeletons::GetLocals(test.level, removed).empty());

    INFO("a SkeletonPose added again makes a fresh instance");
    removed.AddComponent<Fenrir::SkeletonPose>(Fenrir::SkeletonPose{chain});
    test.RunFrame();
    REQUIRE(Skeletons::GetPoseState(test.level, removed) == Fenrir::PoseState::Ready);
}

TEST_CASE("an empty or stale skeleton handle is Unavailable, with empty reads, logged once", "[skeleton][instances]")
{
    SkeletonApp test;
    const Fenrir::AssetHandle<Fenrir::Skeleton> chain = SkeletonTest::AddChain(test);
    const Entity empty = SkeletonTest::SkeletonEntity(test.level, {});
    const Entity stale = SkeletonTest::SkeletonEntity(test.level, chain);
    test.RunFrame();
    REQUIRE(Skeletons::GetPoseState(test.level, stale) == Fenrir::PoseState::Ready);

    REQUIRE(test.store.Unload(chain));
    for (int frame = 0; frame < 3; ++frame)
    {
        test.RunFrame();
    }

    INFO("both are Unavailable");
    REQUIRE(Skeletons::GetPoseState(test.level, empty) == Fenrir::PoseState::Unavailable);
    REQUIRE(Skeletons::GetPoseState(test.level, stale) == Fenrir::PoseState::Unavailable);

    INFO("every read is empty");
    REQUIRE(Skeletons::GetLocals(test.level, stale).empty());
    REQUIRE(Skeletons::GetBaseLocals(test.level, stale).empty());
    REQUIRE(Skeletons::GetModelMatrices(test.level, stale).empty());
    REQUIRE(Skeletons::GetPalette(test.level, stale).empty());
    REQUIRE(Skeletons::EditBasePose(test.level, stale).empty());
    REQUIRE(!Skeletons::GetBoneWorldMatrix(test.level, stale, 0).has_value());
    REQUIRE(!Skeletons::GetModel(test.level, stale, 0).has_value());
    REQUIRE(Skeletons::FindBone(test.level, stale, "root") == Fenrir::NoBone);
    REQUIRE(Skeletons::GetPoseGeneration(test.level, stale) == 0);

    INFO("each entity's problem is logged once over three frames");
    REQUIRE(test.Warnings("unavailable") == 2);
}

TEST_CASE("an Unavailable instance rebuilds once its SkeletonPose names a loaded skeleton", "[skeleton][instances]")
{
    SkeletonApp test;
    Entity skeleton = SkeletonTest::SkeletonEntity(test.level, {});
    test.RunFrame();
    REQUIRE(Skeletons::GetPoseState(test.level, skeleton) == Fenrir::PoseState::Unavailable);

    skeleton.GetComponent<Fenrir::SkeletonPose>().skeleton = SkeletonTest::AddChain(test);
    test.RunFrame();

    INFO("it is Ready, its bones found by name again");
    REQUIRE(Skeletons::GetPoseState(test.level, skeleton) == Fenrir::PoseState::Ready);
    REQUIRE(Skeletons::FindBone(test.level, skeleton, "tip") == 2);

    INFO("losing it again is logged again: once per time it becomes unavailable");
    skeleton.GetComponent<Fenrir::SkeletonPose>().skeleton = {};
    test.RunFrame();
    REQUIRE(test.Warnings("unavailable") == 2);
}

TEST_CASE("spans hold one entry per bone and read the pose as it is now", "[skeleton][instances]")
{
    SkeletonApp test;
    const Entity skeleton = SkeletonTest::SkeletonEntity(test.level, SkeletonTest::AddChain(test));
    test.RunFrame();

    INFO("one entry per bone");
    REQUIRE(Skeletons::GetLocals(test.level, skeleton).size() == 3);
    REQUIRE(Skeletons::GetModelMatrices(test.level, skeleton).size() == 3);
    REQUIRE(Skeletons::GetBaseLocals(test.level, skeleton).size() == 3);

    INFO("a span read again after the pose changed sees the new pose");
    Skeletons::EditBasePose(test.level, skeleton)[0].pos = {0.0f, 2.0f, 0.0f};
    test.RunFrame();
    REQUIRE(Near(Skeletons::GetLocals(test.level, skeleton)[0].pos, {0.0f, 2.0f, 0.0f}));
    REQUIRE(Near(Translation(Skeletons::GetModelMatrices(test.level, skeleton)[2]), {0.0f, 4.0f, 0.0f}));
}

TEST_CASE("base reads see EditBasePose at once; final reads see it after the pose pipeline", "[skeleton][instances]")
{
    SkeletonApp test;
    const Entity skeleton = SkeletonTest::SkeletonEntity(test.level, SkeletonTest::AddChain(test));
    test.RunFrame();

    Skeletons::EditBasePose(test.level, skeleton)[0].pos = {0.0f, 2.0f, 0.0f};

    INFO("the base model matrix is computed from the base pose on read");
    REQUIRE(Near(Translation(*Skeletons::GetBaseModel(test.level, skeleton, 2)), {0.0f, 4.0f, 0.0f}));
    REQUIRE(Near(Skeletons::GetBaseLocal(test.level, skeleton, 0)->pos, {0.0f, 2.0f, 0.0f}));
    INFO("the final pose waits for the pipeline");
    REQUIRE(Near(Translation(*Skeletons::GetModel(test.level, skeleton, 2)), {0.0f, 3.0f, 0.0f}));

    test.RunFrame();
    REQUIRE(Near(Translation(*Skeletons::GetModel(test.level, skeleton, 2)), {0.0f, 4.0f, 0.0f}));

    INFO("a skeleton posed by hand keeps its base pose frame after frame");
    test.RunFrame();
    test.RunFrame();
    REQUIRE(Near(Translation(*Skeletons::GetModel(test.level, skeleton, 2)), {0.0f, 4.0f, 0.0f}));
}

TEST_CASE("both pose kinds read the render pose on a frame-posed skeleton", "[skeleton][instances]")
{
    SkeletonApp test;
    const Entity skeleton = SkeletonTest::SkeletonEntity(test.level, SkeletonTest::AddChain(test));
    test.RunFrame();
    Skeletons::EditBasePose(test.level, skeleton)[1].rot = SkeletonTest::QuarterTurnZ();
    test.RunFrame();

    using Fenrir::PoseKind;
    INFO("Simulation and Render agree");
    REQUIRE(Near(Translation(*Skeletons::GetBaseModel(test.level, skeleton, 2, PoseKind::Simulation)),
                 Translation(*Skeletons::GetBaseModel(test.level, skeleton, 2, PoseKind::Render))));
    REQUIRE(Near(Skeletons::GetBaseLocal(test.level, skeleton, 1, PoseKind::Simulation)->rot,
                 SkeletonTest::QuarterTurnZ()));
    REQUIRE(Near(Translation(*Skeletons::GetBoneWorldMatrix(test.level, skeleton, 2, PoseKind::Simulation)),
                 {-1.0f, 2.0f, 0.0f}));
}
