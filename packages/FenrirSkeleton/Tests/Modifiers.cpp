// Pose modifiers through PoseEdit (ADR-0033): local, model and world targets, channel masks, weights, sequential
// composition, lazy model matrices, degenerate inverses, shear, and clean instances skipping prepare and finalise.
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "SkeletonTest.hpp"

using Fenrir::Entity;
using Fenrir::PoseSpace;
using Fenrir::Skeletons;
using Fenrir::Target;
using SkeletonTest::Near;
using SkeletonTest::SkeletonApp;
using SkeletonTest::Translation;

namespace
{
    Target At(PoseSpace space, Fenrir::Math::Vec3 pos, float weight = 1.0f,
              Fenrir::Channels channels = Fenrir::Channels::All)
    {
        Target target;
        target.space = space;
        target.trs.pos = pos;
        target.channels = channels;
        target.weight = weight;
        return target;
    }

    // A chain skeleton with one modifier, `edit`, run on it each frame.
    struct Modified
    {
        SkeletonApp test;
        Entity skeleton = SkeletonTest::SkeletonEntity(test.level, SkeletonTest::AddChain(test));

        explicit Modified(std::function<void(Fenrir::PoseEdit&)> edit)
        {
            test.skeletons.AddModifier("test", 0, [this, edit = std::move(edit)](Fenrir::ModifierContext& context) {
                Fenrir::PoseEdit pose = context.Edit(skeleton);
                REQUIRE(!pose.IsEmpty());
                edit(pose);
            });
        }

        [[nodiscard]] Fenrir::Math::Vec3 ModelPos(std::int32_t bone)
        {
            return Translation(*Skeletons::GetModel(test.level, skeleton, bone));
        }
    };
} // namespace

TEST_CASE("a local target sets the bone's local transform, and its children follow", "[skeleton][modifiers]")
{
    Modified run([](Fenrir::PoseEdit& pose) {
        Target target = At(PoseSpace::Local, {0.0f, 1.0f, 0.0f});
        target.trs.rot = SkeletonTest::QuarterTurnZ();
        pose.Write(1, target);
    });
    run.test.RunFrame();

    INFO("mid turned a quarter about +Z carries tip to (-1, 2, 0)");
    REQUIRE(Near(run.ModelPos(2), {-1.0f, 2.0f, 0.0f}));
    INFO("the base pose is untouched");
    REQUIRE(Near(Skeletons::GetBaseLocal(run.test.level, run.skeleton, 1)->rot, Fenrir::Math::Quat(1, 0, 0, 0)));
}

TEST_CASE("a model target puts the bone at that model-space transform through its parent", "[skeleton][modifiers]")
{
    Modified run([](Fenrir::PoseEdit& pose) { pose.Write(2, At(PoseSpace::Model, {5.0f, 5.0f, 0.0f})); });
    run.test.RunFrame();

    INFO("the tip's model matrix is the target's");
    REQUIRE(Near(run.ModelPos(2), {5.0f, 5.0f, 0.0f}));
    INFO("so its local is the target relative to mid at (0, 2, 0)");
    REQUIRE(Near(Skeletons::GetLocal(run.test.level, run.skeleton, 2)->pos, {5.0f, 3.0f, 0.0f}));
}

TEST_CASE("a world target goes through the skeleton entity's inverse world matrix", "[skeleton][modifiers]")
{
    Modified run([](Fenrir::PoseEdit& pose) { pose.Write(2, At(PoseSpace::World, {10.0f, 3.0f, 1.0f})); });
    Fenrir::Transform& placed = run.skeleton.GetComponent<Fenrir::Transform>();
    placed.pos = {10.0f, 0.0f, 0.0f};
    run.test.RunFrame();

    INFO("the tip lands on the world target");
    REQUIRE(Near(Translation(*Skeletons::GetBoneWorldMatrix(run.test.level, run.skeleton, 2)), {10.0f, 3.0f, 1.0f}));
    INFO("which is (0, 3, 1) in model space");
    REQUIRE(Near(run.ModelPos(2), {0.0f, 3.0f, 1.0f}));
}

TEST_CASE("a target writes only its masked channels", "[skeleton][modifiers]")
{
    Modified run([](Fenrir::PoseEdit& pose) {
        Target target = At(PoseSpace::Local, {9.0f, 9.0f, 9.0f}, 1.0f, Fenrir::Channels::Rotation);
        target.trs.rot = SkeletonTest::QuarterTurnZ();
        target.trs.scale = {3.0f, 3.0f, 3.0f};
        pose.Write(1, target);
    });
    run.test.RunFrame();

    const Fenrir::Transform mid = *Skeletons::GetLocal(run.test.level, run.skeleton, 1);
    INFO("the rotation is written; translation and scale keep the base pose's");
    REQUIRE(Near(mid.rot, SkeletonTest::QuarterTurnZ()));
    REQUIRE(Near(mid.pos, {0.0f, 1.0f, 0.0f}));
    REQUIRE(Near(mid.scale, {1.0f, 1.0f, 1.0f}));
}

TEST_CASE("a target's weight blends towards it, clamped to [0, 1]", "[skeleton][modifiers]")
{
    Modified run([](Fenrir::PoseEdit& pose) {
        pose.Write(0, At(PoseSpace::Local, {4.0f, 1.0f, 0.0f}, 0.5f));
        pose.Write(1, At(PoseSpace::Local, {2.0f, 1.0f, 0.0f}, 7.0f));
        pose.Write(2, At(PoseSpace::Local, {2.0f, 1.0f, 0.0f}, -1.0f));
        Target turn = At(PoseSpace::Local, {0.0f, 1.0f, 0.0f}, 0.5f, Fenrir::Channels::Rotation);
        turn.trs.rot = SkeletonTest::QuarterTurnZ();
        pose.Write(2, turn);
    });
    run.test.RunFrame();

    INFO("half way from (0, 1, 0) to (4, 1, 0) is (2, 1, 0)");
    REQUIRE(Near(Skeletons::GetLocal(run.test.level, run.skeleton, 0)->pos, {2.0f, 1.0f, 0.0f}));
    INFO("a weight over 1 writes the target exactly; one under 0 writes nothing");
    REQUIRE(Near(Skeletons::GetLocal(run.test.level, run.skeleton, 1)->pos, {2.0f, 1.0f, 0.0f}));
    REQUIRE(Near(Skeletons::GetLocal(run.test.level, run.skeleton, 2)->pos, {0.0f, 1.0f, 0.0f}));
    INFO("rotation blends half way: an eighth of a turn");
    const Fenrir::Math::Quat eighth =
        Fenrir::Math::AngleAxis(Fenrir::Math::DegToRad(45.0f), Fenrir::Math::Vec3(0.0f, 0.0f, 1.0f));
    REQUIRE(Near(Skeletons::GetLocal(run.test.level, run.skeleton, 2)->rot, eighth));
}

TEST_CASE("writes compose in order, within a modifier and across modifiers", "[skeleton][modifiers]")
{
    SkeletonApp test;
    const Entity skeleton = SkeletonTest::SkeletonEntity(test.level, SkeletonTest::AddChain(test));
    auto halfway = [&](Fenrir::ModifierContext& context) {
        Fenrir::PoseEdit pose = context.Edit(skeleton);
        pose.Write(0, At(PoseSpace::Local, {4.0f, 1.0f, 0.0f}, 0.5f));
    };
    test.skeletons.AddModifier("first", 0, [&](Fenrir::ModifierContext& context) {
        halfway(context);
        halfway(context);
    });
    test.skeletons.AddModifier("second", 1, halfway);
    test.RunFrame();

    INFO("each write blends from the one before: 0 to 2, to 3, to 3.5");
    REQUIRE(Near(Skeletons::GetLocal(test.level, skeleton, 0)->pos, {3.5f, 1.0f, 0.0f}));
}

TEST_CASE("a model read after a write sees it at once, with the model matrices below recomputed",
          "[skeleton][modifiers]")
{
    std::optional<Fenrir::Math::Mat4> tipBefore;
    std::optional<Fenrir::Math::Mat4> tipAfter;
    Modified run([&](Fenrir::PoseEdit& pose) {
        tipBefore = pose.Model(2);
        Target target = At(PoseSpace::Local, {0.0f, 1.0f, 0.0f}, 1.0f, Fenrir::Channels::Rotation);
        target.trs.rot = SkeletonTest::QuarterTurnZ();
        pose.Write(1, target);
        tipAfter = pose.Model(2);
    });
    run.test.RunFrame();

    INFO("before the write the tip is at rest; after it, mid's turn has moved it");
    REQUIRE(Near(Translation(*tipBefore), {0.0f, 3.0f, 0.0f}));
    REQUIRE(Near(Translation(*tipAfter), {-1.0f, 2.0f, 0.0f}));
}

TEST_CASE("a model target under a later-moved ancestor moves with it: targets are met when written",
          "[skeleton][modifiers]")
{
    Modified run([](Fenrir::PoseEdit& pose) {
        pose.Write(2, At(PoseSpace::Model, {0.0f, 3.0f, 2.0f}));
        pose.Write(0, At(PoseSpace::Local, {1.0f, 1.0f, 0.0f}));
    });
    run.test.RunFrame();

    INFO("moving the root after the tip's write carries the tip one unit along x");
    REQUIRE(Near(run.ModelPos(2), {1.0f, 3.0f, 2.0f}));
}

TEST_CASE("a world target on a skeleton entity with no inverse is skipped, warned with a rate limit",
          "[skeleton][modifiers]")
{
    bool written = true;
    Modified run([&](Fenrir::PoseEdit& pose) { written = pose.Write(2, At(PoseSpace::World, {1.0f, 1.0f, 1.0f})); });
    run.skeleton.GetComponent<Fenrir::Transform>().scale = {0.0f, 1.0f, 1.0f};
    for (int frame = 0; frame < 10; ++frame)
    {
        run.test.RunFrame();
    }

    INFO("the target is skipped and the bone keeps its pose");
    REQUIRE(!written);
    REQUIRE(Near(run.ModelPos(2), {0.0f, 3.0f, 0.0f}));
    INFO("ten frames held degenerate give one warning, naming the bone");
    REQUIRE(run.test.Warnings("on bone 2") == 1);
}

TEST_CASE("a model target under a parent bone with no inverse is skipped and warned", "[skeleton][modifiers]")
{
    bool written = true;
    Modified run([&](Fenrir::PoseEdit& pose) { written = pose.Write(2, At(PoseSpace::Model, {1.0f, 1.0f, 1.0f})); });
    run.test.RunFrame();
    Skeletons::EditBasePose(run.test.level, run.skeleton)[1].scale = {1.0f, 0.0f, 1.0f};
    run.test.RunFrame();

    INFO("the write is refused with a warning about the parent's model matrix");
    REQUIRE(!written);
    REQUIRE(run.test.Warnings("parent bone's model matrix has no inverse") == 1);
}

TEST_CASE("under a parent's rotation and non-uniform scale a model target keeps its position and drops shear",
          "[skeleton][modifiers]")
{
    Modified run([](Fenrir::PoseEdit& pose) { pose.Write(2, At(PoseSpace::Model, {1.0f, 1.0f, 0.0f})); });
    run.test.RunFrame();
    std::span<Fenrir::Transform> base = Skeletons::EditBasePose(run.test.level, run.skeleton);
    base[1].rot = Fenrir::Math::AngleAxis(Fenrir::Math::DegToRad(45.0f), Fenrir::Math::Vec3(0.0f, 0.0f, 1.0f));
    base[1].scale = {2.0f, 1.0f, 1.0f};
    run.test.RunFrame();

    const Fenrir::Transform tip = *Skeletons::GetLocal(run.test.level, run.skeleton, 2);
    INFO("the local transform is plain TRS: finite, with a unit rotation");
    REQUIRE(std::isfinite(tip.pos.x));
    REQUIRE(std::isfinite(tip.scale.x));
    REQUIRE(Near(glm::length(tip.rot), 1.0f));
    INFO("translation needs no shear, so the tip is exactly where the target put it");
    REQUIRE(Near(run.ModelPos(2), {1.0f, 1.0f, 0.0f}));
}

TEST_CASE("modifier writes do not build up across frames: each frame starts from the base pose",
          "[skeleton][modifiers]")
{
    SkeletonApp test;
    const Entity skeleton = SkeletonTest::SkeletonEntity(test.level, SkeletonTest::AddChain(test));
    const Fenrir::PoseStageHandle nudge = test.skeletons.AddModifier("nudge", 0, [&](Fenrir::ModifierContext& context) {
        Fenrir::PoseEdit pose = context.Edit(skeleton);
        const Fenrir::Transform root = *pose.Local(0);
        pose.Write(0, At(PoseSpace::Local, root.pos + Fenrir::Math::Vec3(1.0f, 0.0f, 0.0f)));
    });
    std::uint64_t last = 0;
    for (int frame = 0; frame < 3; ++frame)
    {
        test.RunFrame();
        INFO("an edited instance gets a new generation every frame");
        REQUIRE(Skeletons::GetPoseGeneration(test.level, skeleton) > last);
        last = Skeletons::GetPoseGeneration(test.level, skeleton);
    }

    INFO("after three frames the root is one unit along, not three");
    REQUIRE(Near(Skeletons::GetLocal(test.level, skeleton, 0)->pos, {1.0f, 1.0f, 0.0f}));

    INFO("with the modifier gone, the next frame's final pose is the base pose again");
    REQUIRE(test.skeletons.Remove(nudge));
    test.RunFrame();
    REQUIRE(Near(Skeletons::GetLocal(test.level, skeleton, 0)->pos, {0.0f, 1.0f, 0.0f}));
    REQUIRE(Skeletons::GetPoseGeneration(test.level, skeleton) > last);
}

TEST_CASE("clean instances that no modifier edits keep their pose and generation", "[skeleton][modifiers]")
{
    SkeletonApp test;
    const Fenrir::AssetHandle<Fenrir::Skeleton> chain = SkeletonTest::AddChain(test);
    const Entity edited = SkeletonTest::SkeletonEntity(test.level, chain);
    const Entity clean = SkeletonTest::SkeletonEntity(test.level, chain);
    test.skeletons.AddModifier("one only", 0, [&](Fenrir::ModifierContext& context) {
        Fenrir::PoseEdit pose = context.Edit(edited);
        pose.Write(0, At(PoseSpace::Local, {2.0f, 0.0f, 0.0f}));
    });
    test.RunFrame();
    const std::uint64_t cleanGeneration = Skeletons::GetPoseGeneration(test.level, clean);
    test.RunFrame();
    test.RunFrame();

    INFO("the clean instance is skipped: same generation, rest pose");
    REQUIRE(Skeletons::GetPoseGeneration(test.level, clean) == cleanGeneration);
    REQUIRE(Near(Translation(*Skeletons::GetModel(test.level, clean, 2)), {0.0f, 3.0f, 0.0f}));
}

TEST_CASE("an edit on an instance that is not Ready, or kept past its call, is empty", "[skeleton][modifiers]")
{
    SkeletonApp test;
    const Entity skeleton = SkeletonTest::SkeletonEntity(test.level, SkeletonTest::AddChain(test));
    const Entity unavailable = SkeletonTest::SkeletonEntity(test.level, {});
    Fenrir::PoseEdit kept;
    bool emptyOnUnavailable = false;
    test.skeletons.AddModifier("keeper", 0, [&](Fenrir::ModifierContext& context) {
        emptyOnUnavailable = context.Edit(unavailable).IsEmpty();
        kept = context.Edit(skeleton);
    });
    bool keptWrote = true;
    test.skeletons.AddModifier("late user", 1, [&](Fenrir::ModifierContext&) {
        keptWrote = kept.Write(0, At(PoseSpace::Local, {7.0f, 0.0f, 0.0f}));
    });
    test.RunFrame();

    INFO("an Unavailable instance's edit is empty");
    REQUIRE(emptyOnUnavailable);
    INFO("an edit used in a later modifier call writes nothing, and reads nothing");
    REQUIRE(!keptWrote);
    REQUIRE(kept.IsEmpty());
    REQUIRE(kept.Base().empty());
    REQUIRE(Near(Skeletons::GetLocal(test.level, skeleton, 0)->pos, {0.0f, 1.0f, 0.0f}));
}
