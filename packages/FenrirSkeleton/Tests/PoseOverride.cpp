// The pose override and override weights (ADR-0033, ADR-0038), seam 8's ragdoll handover test with no physics:
// plain entities with a Transform stand in for bodies, and a test glue writes their world transforms into the chain
// skeleton as world targets in Update. The skeleton entity stands at (10, 0, 0), so world and model space differ.
#include <cstdint>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirScheduler/Scheduler.hpp"
#include "SkeletonTest.hpp"

using Fenrir::Entity;
using Fenrir::OverrideScope;
using Fenrir::PoseSpace;
using Fenrir::Skeletons;
using Fenrir::Target;
using Fenrir::Math::Vec3;
using SkeletonTest::Near;
using SkeletonTest::SkeletonApp;
using SkeletonTest::Translation;

namespace
{
    constexpr std::int32_t Root = 0;
    constexpr std::int32_t Mid = 1;
    constexpr std::int32_t Tip = 2;

    // The chain at (10, 0, 0), with fake bodies for its root and mid, and a glue that maps them while `simulating`.
    struct Ragdoll
    {
        SkeletonApp test;
        Entity skeleton = SkeletonTest::SkeletonEntity(test.level, SkeletonTest::AddChain(test));
        Entity rootBody = test.level.GetEntityList().CreateEntity();
        Entity midBody = test.level.GetEntityList().CreateEntity();
        bool simulating = true;

        Ragdoll()
        {
            skeleton.GetComponent<Fenrir::Transform>().pos = {10.0f, 0.0f, 0.0f};
            // Ready before the first Update, as a spawned skeleton is.
            test.skeletons.Ensure(test.level, skeleton);
            // The bodies lie on their side: the root two units along x, the mid a quarter turn about z above it.
            rootBody.GetComponent<Fenrir::Transform>().pos = {12.0f, 1.0f, 0.0f};
            Fenrir::Transform& mid = midBody.GetComponent<Fenrir::Transform>();
            mid.pos = {11.0f, 1.0f, 0.0f};
            mid.rot = SkeletonTest::QuarterTurnZ();
            // The glue, in Update: each mapped bone's world target from its body.
            test.app.AddSequentialSystem(Fenrir::SchedulePriority::Update, [this](Fenrir::App&) {
                if (!simulating)
                {
                    return;
                }
                Write(Root, rootBody);
                Write(Mid, midBody);
            });
        }

        void Write(std::int32_t bone, const Entity& body)
        {
            Target target;
            target.space = PoseSpace::World;
            target.trs = body.GetComponent<Fenrir::Transform>();
            Skeletons::SetOverride(test.level, skeleton, bone, target);
        }

        [[nodiscard]] Fenrir::Math::Mat4 World(std::int32_t bone)
        {
            return Skeletons::GetBoneWorldMatrix(test.level, skeleton, bone).value_or(Fenrir::Math::Mat4(0.0f));
        }
    };
} // namespace

TEST_CASE("mapped bones land on their bodies, an unmapped child follows, and base reads ignore overrides",
          "[skeleton][override]")
{
    Ragdoll run;
    run.test.RunFrame();

    INFO("the root and mid are where their bodies are, the mid turned with its body");
    REQUIRE(Near(Translation(run.World(Root)), {12.0f, 1.0f, 0.0f}));
    REQUIRE(Near(Translation(run.World(Mid)), {11.0f, 1.0f, 0.0f}));
    REQUIRE(Near(Skeletons::GetLocal(run.test.level, run.skeleton, Mid)->rot, SkeletonTest::QuarterTurnZ()));
    INFO("the tip, with no entry, keeps its local unit up and follows the turned mid: one unit along -x");
    REQUIRE(Near(Translation(run.World(Tip)), {10.0f, 1.0f, 0.0f}));
    REQUIRE(Near(Skeletons::GetLocal(run.test.level, run.skeleton, Tip)->pos, {0.0f, 1.0f, 0.0f}));
    INFO("the base pose is still the rest pose");
    REQUIRE(Near(Skeletons::GetBaseLocal(run.test.level, run.skeleton, Root)->pos, {0.0f, 1.0f, 0.0f}));
    REQUIRE(Near(Translation(*Skeletons::GetBaseModel(run.test.level, run.skeleton, Tip)), {0.0f, 3.0f, 0.0f}));
}

TEST_CASE("a bone with no entry keeps its animated pose", "[skeleton][override]")
{
    Ragdoll run;
    // The animation turns the tip a quarter about z each frame, before the pipeline.
    run.test.app.AddSequentialSystem(Fenrir::SchedulePriority::Update, [&](Fenrir::App&) {
        Skeletons::EditBasePose(run.test.level, run.skeleton)[Tip].rot = SkeletonTest::QuarterTurnZ();
    });
    run.test.RunFrame();
    run.test.RunFrame();

    REQUIRE(Near(Skeletons::GetLocal(run.test.level, run.skeleton, Tip)->rot, SkeletonTest::QuarterTurnZ()));
}

TEST_CASE("a subtree weight of 0.5 blends halfway, and a later per-bone weight wins", "[skeleton][override]")
{
    Ragdoll run;
    run.test.RunFrame();
    REQUIRE(Skeletons::SetOverrideWeight(run.test.level, run.skeleton, Root, 0.5f, OverrideScope::BoneSubtree));

    INFO("the subtree assignment gave each bone 0.5");
    REQUIRE(Skeletons::GetOverrideWeight(run.test.level, run.skeleton, Root) == 0.5f);
    REQUIRE(Skeletons::GetOverrideWeight(run.test.level, run.skeleton, Mid) == 0.5f);
    REQUIRE(Skeletons::GetOverrideWeight(run.test.level, run.skeleton, Tip) == 0.5f);
    run.test.RunFrame();
    INFO("the root halfway from its local (0, 1, 0) to the body's (2, 1, 0) in model space");
    REQUIRE(Near(Skeletons::GetLocal(run.test.level, run.skeleton, Root)->pos, {1.0f, 1.0f, 0.0f}));

    REQUIRE(Skeletons::SetOverrideWeight(run.test.level, run.skeleton, Mid, 1.0f));
    run.test.RunFrame();
    INFO("the later per-bone weight replaces the mid's, nothing multiplied");
    REQUIRE(Skeletons::GetOverrideWeight(run.test.level, run.skeleton, Mid) == 1.0f);
    REQUIRE(Skeletons::GetOverrideWeight(run.test.level, run.skeleton, Tip) == 0.5f);
    REQUIRE(Near(Translation(run.World(Mid)), {11.0f, 1.0f, 0.0f}));

    REQUIRE(Skeletons::SetOverrideWeight(run.test.level, run.skeleton, Root, 0.0f, OverrideScope::BoneSubtree));
    run.test.RunFrame();
    INFO("at 0 every entry is skipped: the rest pose again");
    REQUIRE(Near(Translation(run.World(Tip)), {10.0f, 3.0f, 0.0f}));
    REQUIRE(Skeletons::ClearOverrideWeight(run.test.level, run.skeleton, Root, OverrideScope::BoneSubtree));
    REQUIRE(Skeletons::GetOverrideWeight(run.test.level, run.skeleton, Tip) == 1.0f);
}

TEST_CASE("clearing entries keeps the weights, and a rebuild drops both", "[skeleton][override]")
{
    Ragdoll run;
    run.test.RunFrame();
    Skeletons::SetOverrideWeight(run.test.level, run.skeleton, Mid, 0.25f);
    run.simulating = false;
    REQUIRE(Skeletons::ClearOverride(run.test.level, run.skeleton, Root));
    REQUIRE_FALSE(Skeletons::ClearOverride(run.test.level, run.skeleton, Root));
    REQUIRE(Skeletons::ClearOverrides(run.test.level, run.skeleton));
    run.test.RunFrame();

    INFO("no entries: the rest pose; the weight stays");
    REQUIRE_FALSE(Skeletons::GetOverride(run.test.level, run.skeleton, Mid).has_value());
    REQUIRE(Near(Translation(run.World(Tip)), {10.0f, 3.0f, 0.0f}));
    REQUIRE(Skeletons::GetOverrideWeight(run.test.level, run.skeleton, Mid) == 0.25f);

    run.simulating = true;
    run.test.RunFrame();
    REQUIRE(Skeletons::GetOverride(run.test.level, run.skeleton, Mid).has_value());
    // A new revision of the skeleton rebuilds the instance.
    const auto handle = run.skeleton.GetComponent<Fenrir::SkeletonPose>().skeleton;
    REQUIRE(run.test.store.Replace(handle, SkeletonTest::ChainData()));
    run.simulating = false;
    run.test.RunFrame();
    INFO("the rebuild dropped the entries and the weights");
    REQUIRE_FALSE(Skeletons::GetOverride(run.test.level, run.skeleton, Root).has_value());
    REQUIRE(Skeletons::GetOverrideWeight(run.test.level, run.skeleton, Mid) == 1.0f);
    REQUIRE(Near(Translation(run.World(Tip)), {10.0f, 3.0f, 0.0f}));
}

TEST_CASE("PoseOverride runs at order 1000: an earlier modifier is overridden and a later one sees its result",
          "[skeleton][override]")
{
    Ragdoll run;
    run.test.skeletons.AddModifier("before", Fenrir::ModifierOrder::Override - 1, [&](Fenrir::ModifierContext& c) {
        Target target;
        target.trs.pos = {0.0f, 5.0f, 0.0f};
        c.Edit(run.skeleton).Write(Root, target);
    });
    std::optional<Vec3> seenAfter;
    std::optional<Vec3> seenTied;
    run.test.skeletons.AddModifier("after", Fenrir::ModifierOrder::Override + 1, [&](Fenrir::ModifierContext& c) {
        seenAfter = Translation(*c.Edit(run.skeleton).Model(Root));
    });
    // Added after PoseOverride at its order: ties run in the order added.
    run.test.skeletons.AddModifier("tied", Fenrir::ModifierOrder::Override, [&](Fenrir::ModifierContext& c) {
        seenTied = Translation(*c.Edit(run.skeleton).Model(Root));
    });

    run.test.RunFrame();

    INFO("the root is on its body in model space, (2, 1, 0), whatever the earlier modifier wrote");
    REQUIRE(seenAfter.has_value());
    REQUIRE(Near(*seenAfter, {2.0f, 1.0f, 0.0f}));
    REQUIRE(seenTied.has_value());
    REQUIRE(Near(*seenTied, {2.0f, 1.0f, 0.0f}));
}

TEST_CASE("the override calls do nothing on an entity with no Ready instance", "[skeleton][override]")
{
    SkeletonApp test;
    const Entity plain = test.level.GetEntityList().CreateEntity();
    REQUIRE_FALSE(Skeletons::SetOverride(test.level, plain, 0, Target{}));
    REQUIRE_FALSE(Skeletons::SetOverrideWeight(test.level, plain, 0, 0.5f));
    REQUIRE(Skeletons::GetOverrideWeight(test.level, plain, 0) == 1.0f);
    REQUIRE(Skeletons::GetSubtree(test.level, plain, 0).empty());
    test.RunFrame();
}

TEST_CASE("GetSubtree lists the bone and every bone below it", "[skeleton][override]")
{
    SkeletonApp test;
    const Entity skeleton = SkeletonTest::SkeletonEntity(test.level, SkeletonTest::AddChain(test));
    test.RunFrame();
    REQUIRE(Skeletons::GetSubtree(test.level, skeleton, Mid) == std::vector<std::int32_t>{Mid, Tip});
    REQUIRE(Skeletons::GetSubtree(test.level, skeleton, 7).empty());
}
