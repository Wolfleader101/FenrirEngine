// Skeletons from C# (spec seam 7), through the fixture's Probe, Reads and Attaching classes and its stage systems: a C#
// Modify system between two C++ modifiers by order, a stale pose edit writing nothing, a C# Late system reading this
// frame's bone, PoseStages.Timing in both passes, Simulation reads matching C++ on a tick-posed skeleton, the reads and
// copies agreeing with C++, base-pose writes, overrides, the asset's queries, attachments, Detach and SpawnModel
// through the change buffer. Each case makes its own App, so the stage systems are armed on its skeleton alone.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/Model.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirSkeleton/BoneAttachment.hpp"
#include "FenrirSkeleton/PoseStages.hpp"
#include "SkeletonScriptingApp.hpp"

using namespace Fenrir;
using namespace Fenrir::Testing;

namespace
{
    constexpr const char* ProbeType = "Fenrir.SkeletonScriptFixture.Probe";
    constexpr const char* ReadsType = "Fenrir.SkeletonScriptFixture.Reads";
    constexpr const char* AttachingType = "Fenrir.SkeletonScriptFixture.Attaching";
    constexpr std::int32_t Mid = 1;
    constexpr std::int32_t Tip = 2;
    constexpr std::uint32_t NoEntity = 0xFFFFFFFFu;

    Scene& Active(SkeletonScriptingApp& test)
    {
        return test.Get().GetActiveScene();
    }

    // The bone's translation's Y in `matrix`, a model or world matrix.
    float TranslationY(const std::optional<Math::Mat4>& matrix)
    {
        REQUIRE(matrix.has_value());
        return (*matrix)[3][1];
    }

    // A local-space target moving a bone's X to `x`, keeping the rest.
    Target LocalX(Transform local, float x)
    {
        local.pos.x = x;
        return Target{PoseSpace::Local, local, Channels::All, 1.0f};
    }
} // namespace

TEST_CASE("A C# Modify stage system runs between C++ modifiers by order, and a stale edit writes nothing",
          "[skeleton][scripting][stages]")
{
    SkeletonScriptingApp test;
    const Entity skeleton = test.SpawnChain(Math::Vec3(0.0f));
    float lastSaw = -1.0f;
    (void)test.skeletons.AddModifier("first", 100, [&](ModifierContext& context) {
        PoseEdit edit = context.Edit(skeleton);
        (void)edit.Write(Tip, LocalX(*edit.Local(Tip), 1.0f));
    });
    (void)test.skeletons.AddModifier(
        "last", 300, [&](ModifierContext& context) { lastSaw = context.Edit(skeleton).Local(Tip)->pos.x; });
    test.Call<void>(ProbeType, "Arm", skeleton.GetId(), Tip, NoEntity);

    test.Frame();
    INFO("The C# modifier at 200 should see the first C++ modifier's write, and the last see its own");
    CHECK(test.Call<float>(ProbeType, "GetModifierSaw") == Catch::Approx(1.0f));
    CHECK(test.Call<bool>(ProbeType, "GetModifierWrote"));
    CHECK(lastSaw == Catch::Approx(2.0f));

    // The next frame's Modify writes through the token the last one kept: stale, so the bone stays at 2.
    test.Frame();
    CHECK(test.Call<int>(ProbeType, "GetStaleTries") == 1);
    CHECK_FALSE(test.Call<bool>(ProbeType, "GetStaleWrote"));
    CHECK(lastSaw == Catch::Approx(2.0f));
    CHECK(Skeletons::GetLocal(Active(test), skeleton, Tip)->pos.x == Catch::Approx(2.0f));
}

TEST_CASE("A C# Late stage system reads this frame's bone", "[skeleton][scripting][stages]")
{
    SkeletonScriptingApp test;
    const Entity skeleton = test.SpawnChain(Math::Vec3(5.0f, 0.0f, 0.0f));
    Entity follower = test.Entities().CreateEntity();
    int frames = 0;
    // The tip's base local moves up each frame, so last frame's bone would lag a unit behind.
    (void)test.skeletons.AddAnimate("raise", 0, [&](AnimateContext& context) {
        const std::span<Transform> base = Skeletons::EditBasePose(context.GetScene(), skeleton);
        base[Tip].pos.y = static_cast<float>(++frames);
    });
    test.Call<void>(ProbeType, "Arm", skeleton.GetId(), Tip, follower.GetId());

    for (int frame = 0; frame < 3; ++frame)
    {
        test.Frame();
        const std::optional<Math::Mat4> world = Skeletons::GetBoneWorldMatrix(Active(test), skeleton, Tip);
        INFO("Frame " << frame);
        CHECK(follower.GetComponent<Transform>().pos.y == Catch::Approx(TranslationY(world)));
        // The C# Modify system nudges the tip two units along X too, which this frame's bone carries.
        CHECK(follower.GetComponent<Transform>().pos.x == Catch::Approx((*world)[3][0]));
        CHECK(follower.GetComponent<Transform>().pos.x == Catch::Approx(7.0f));
    }
}

TEST_CASE("PoseStages.Timing and Step name the pass in the frame pass and each tick pass, and Simulation reads match "
          "C++",
          "[skeleton][scripting][stages][tick]")
{
    SkeletonScriptingApp test;
    const Entity frameSkeleton = test.SpawnChain(Math::Vec3(0.0f));
    const Entity tickSkeleton = test.SpawnChain(Math::Vec3(0.0f, 0.0f, 3.0f), PoseTiming::Tick);
    (void)frameSkeleton;
    test.Call<void>(ProbeType, "Arm", tickSkeleton.GetId(), Mid, NoEntity);
    test.Call<void>(ProbeType, "WriteInAnimateStage", true);

    const double tickRate = test.Get().GetTime().tickRate;
    const double delta = tickRate * 1.25;
    for (int frame = 0; frame < 4; ++frame)
    {
        test.Frame(delta);
    }
    const int ticks = test.Call<int>(ProbeType, "GetTickPasses");
    CHECK(test.Call<int>(ProbeType, "GetFramePasses") == 4);
    CHECK(ticks >= 4);
    CHECK(test.Call<double>(ProbeType, "GetFrameStep") == Catch::Approx(delta));
    CHECK(test.Call<double>(ProbeType, "GetTickStep") == Catch::Approx(tickRate));
    INFO("The tick-posed skeleton's base pose is written in its own pass only");
    CHECK(test.Call<int>(ProbeType, "GetAnimateWrites") == ticks);
    CHECK(test.Call<int>(ProbeType, "GetAnimateRefusals") == 4);

    test.Call<void>(ProbeType, "Disarm");
    Skeletons::EditBasePose(Active(test), tickSkeleton)[Tip].pos.y = 3.0f;
    test.Frame(delta);
    const auto simulation = static_cast<int>(PoseKind::Simulation);
    const auto render = static_cast<int>(PoseKind::Render);
    for (std::int32_t bone = 0; bone < 3; ++bone)
    {
        INFO("Bone " << bone);
        CHECK(test.Call<float>(ReadsType, "WorldY", tickSkeleton.GetId(), bone, simulation) ==
              Catch::Approx(
                  TranslationY(Skeletons::GetBoneWorldMatrix(Active(test), tickSkeleton, bone, PoseKind::Simulation))));
        CHECK(test.Call<float>(ReadsType, "BaseLocalY", tickSkeleton.GetId(), bone, simulation) ==
              Catch::Approx(Skeletons::GetBaseLocal(Active(test), tickSkeleton, bone, PoseKind::Simulation)->pos.y));
        CHECK(test.Call<float>(ReadsType, "ReadBaseLocalsY", tickSkeleton.GetId(), bone, simulation) ==
              Catch::Approx(Skeletons::GetBaseLocals(Active(test), tickSkeleton, PoseKind::Simulation)[bone].pos.y));
        CHECK(test.Call<float>(ReadsType, "WorldY", tickSkeleton.GetId(), bone, render) ==
              Catch::Approx(TranslationY(Skeletons::GetBoneWorldMatrix(Active(test), tickSkeleton, bone))));
    }
}

TEST_CASE("Per-bone reads and the copies from C# agree with C++", "[skeleton][scripting][reads]")
{
    SkeletonScriptingApp test;
    const Entity skeleton = test.SpawnChain(Math::Vec3(0.0f, 2.0f, 0.0f));
    Skeletons::EditBasePose(Active(test), skeleton)[Mid].pos.y = 2.5f;
    (void)test.skeletons.AddModifier("lift tip", 100, [&](ModifierContext& context) {
        PoseEdit edit = context.Edit(skeleton);
        Transform tip = *edit.Local(Tip);
        tip.pos.y = 0.5f;
        (void)edit.Write(Tip, Target{PoseSpace::Local, tip, Channels::All, 1.0f});
    });
    test.Frame();

    const std::uint32_t id = skeleton.GetId();
    const auto render = static_cast<int>(PoseKind::Render);
    CHECK(test.Call<int>(ReadsType, "PoseState", id) == static_cast<int>(PoseState::Ready));
    CHECK(test.Call<std::uint64_t>(ReadsType, "Generation", id) ==
          Skeletons::GetPoseGeneration(Active(test), skeleton));
    CHECK(test.Call<int>(ReadsType, "FindTip", id) == Tip);
    CHECK(test.Call<int>(ReadsType, "ReadLocalsCount", id, 2) == 2);
    CHECK(test.Call<int>(ReadsType, "ReadLocalsCount", id, 8) == 3);
    for (std::int32_t bone = 0; bone < 3; ++bone)
    {
        INFO("Bone " << bone);
        const float local = Skeletons::GetLocal(Active(test), skeleton, bone)->pos.y;
        const float base = Skeletons::GetBaseLocal(Active(test), skeleton, bone)->pos.y;
        const float model = TranslationY(Skeletons::GetModel(Active(test), skeleton, bone));
        CHECK(test.Call<float>(ReadsType, "LocalY", id, bone) == Catch::Approx(local));
        CHECK(test.Call<float>(ReadsType, "ReadLocalsY", id, bone) == Catch::Approx(local));
        CHECK(test.Call<float>(ReadsType, "BaseLocalY", id, bone, render) == Catch::Approx(base));
        CHECK(test.Call<float>(ReadsType, "ReadBaseLocalsY", id, bone, render) == Catch::Approx(base));
        CHECK(test.Call<float>(ReadsType, "ModelY", id, bone) == Catch::Approx(model));
        CHECK(test.Call<float>(ReadsType, "ReadModelY", id, bone) == Catch::Approx(model));
        CHECK(test.Call<float>(ReadsType, "WorldY", id, bone, render) ==
              Catch::Approx(TranslationY(Skeletons::GetBoneWorldMatrix(Active(test), skeleton, bone))));
    }
    INFO("The final pose and the base pose differ at the tip, and C# sees which is which");
    CHECK(test.Call<float>(ReadsType, "LocalY", id, Tip) == Catch::Approx(0.5f));
    CHECK(test.Call<float>(ReadsType, "BaseLocalY", id, Tip, render) == Catch::Approx(1.0f));
    CHECK(std::isnan(test.Call<float>(ReadsType, "LocalY", id, 3)));
}

TEST_CASE("Base-pose writes, overrides and weights from C# reach the pose", "[skeleton][scripting][writes]")
{
    SkeletonScriptingApp test;
    const Entity skeleton = test.SpawnChain(Math::Vec3(0.0f));
    const std::uint32_t id = skeleton.GetId();

    CHECK(test.Call<int>(ReadsType, "SetBasePoseY", id, 3, 1.5f) == 3);
    CHECK(test.Call<bool>(ReadsType, "SetBaseY", id, Mid, 4.0f));
    CHECK_FALSE(test.Call<bool>(ReadsType, "SetBaseY", id, 7, 4.0f));
    test.Frame();
    CHECK(Skeletons::GetBaseLocal(Active(test), skeleton, 0)->pos.y == Catch::Approx(1.5f));
    CHECK(Skeletons::GetBaseLocal(Active(test), skeleton, Mid)->pos.y == Catch::Approx(4.0f));

    CHECK(test.Call<bool>(ReadsType, "OverrideAt", id, Tip, 3.0f, 9.0f));
    CHECK(test.Call<bool>(ReadsType, "OverrideWeight", id, Mid, 0.5f));
    CHECK(test.Call<float>(ReadsType, "GetOverrideWeight", id, Tip) == Catch::Approx(0.5f));
    test.Frame();
    const std::optional<Math::Mat4> tip = Skeletons::GetModel(Active(test), skeleton, Tip);
    REQUIRE(tip.has_value());
    INFO("Half-weighted, the tip goes halfway from its animated place (0, 7) to the target (3, 9)");
    CHECK((*tip)[3][0] == Catch::Approx(1.5f));
    CHECK((*tip)[3][1] == Catch::Approx(8.0f));
    CHECK(test.Call<bool>(ReadsType, "ClearOverrides", id));
    CHECK_FALSE(Skeletons::GetOverride(Active(test), skeleton, Tip).has_value());
}

TEST_CASE("Pose edits and PoseStages outside their stages do nothing", "[skeleton][scripting][stages]")
{
    SkeletonScriptingApp test;
    const Entity skeleton = test.SpawnChain(Math::Vec3(0.0f));
    test.Frame();
    CHECK_FALSE(test.Call<bool>(ReadsType, "EditOutsideModify", skeleton.GetId(), Tip));
    CHECK(test.Call<int>(ReadsType, "TimingOutsideAnimate") == static_cast<int>(PoseTiming::Frame));
    CHECK(test.Call<double>(ReadsType, "StepOutsideAnimate") == 0.0);
}

TEST_CASE("SkeletonData's queries by handle from C# agree with the asset", "[skeleton][scripting][reads]")
{
    SkeletonScriptingApp test;
    const AssetHandle<Skeleton> chain = test.AddChain();
    CHECK(test.Call<int>(ReadsType, "BoneCount", chain.index, chain.generation) == 3);
    CHECK(test.Call<int>(ReadsType, "Parent", chain.index, chain.generation, Tip) == Mid);
    CHECK(test.Call<int>(ReadsType, "Parent", chain.index, chain.generation, 0) == NoBone);
    CHECK(test.Call<bool>(ReadsType, "IsDescendant", chain.index, chain.generation, Tip, 0));
    CHECK_FALSE(test.Call<bool>(ReadsType, "IsDescendant", chain.index, chain.generation, 0, Tip));
    CHECK(test.Call<std::optional<std::string>>(ReadsType, "EngineName", chain.index, chain.generation, Tip) ==
          std::optional<std::string>("tip"));
    // Mid's subtree is mid and tip: 1 + 2, and two bones.
    CHECK(test.Call<int>(ReadsType, "SubtreeSum", chain.index, chain.generation, Mid) == 203);
    CHECK(test.Call<int>(ReadsType, "BoneCount", chain.index + 7, chain.generation) == 0);
}

TEST_CASE("A BoneAttachment made in C# places its entity, reports its state, and detaches through the change buffer",
          "[skeleton][scripting][attachments]")
{
    SkeletonScriptingApp test;
    const Entity skeleton = test.SpawnChain(Math::Vec3(0.0f));
    Entity held = test.Entities().CreateEntity();
    Entity lost = test.Entities().CreateEntity();

    REQUIRE(test.Call<bool>(AttachingType, "Attach", held.GetId(), skeleton.GetId(), 2));
    REQUIRE(test.Call<bool>(AttachingType, "Attach", lost.GetId(), skeleton.GetId(), 3));
    test.ApplyChanges();
    REQUIRE(held.HasComponent<BoneAttachment>());
    CHECK(held.GetComponent<BoneAttachment>().bone.Get() == std::optional<std::string_view>("tip"));
    test.Frame();
    CHECK(test.Call<int>(AttachingType, "State", held.GetId()) == static_cast<int>(AttachmentState::Attached));
    CHECK(test.Call<int>(AttachingType, "State", lost.GetId()) == static_cast<int>(AttachmentState::BoneNotFound));
    // The tip is three units up, and the offset one along X.
    CHECK(held.GetComponent<Transform>().pos.x == Catch::Approx(1.0f));
    CHECK(held.GetComponent<Transform>().pos.y == Catch::Approx(3.0f));
    CHECK(test.Call<std::optional<std::string>>(AttachingType, "RefusedName") == std::optional<std::string>("tip"));

    CHECK(test.Call<int>(AttachingType, "Detach", held.GetId(), static_cast<int>(DetachMode::KeepWorld), NoEntity) ==
          static_cast<int>(DetachResult::Detached));
    INFO("Detach is structural, so the attachment stays until the changes play back");
    CHECK(held.HasComponent<BoneAttachment>());
    test.ApplyChanges();
    CHECK_FALSE(held.HasComponent<BoneAttachment>());
    CHECK(held.GetComponent<Transform>().pos.y == Catch::Approx(3.0f));
    CHECK(test.Call<int>(AttachingType, "Detach", held.GetId(), static_cast<int>(DetachMode::KeepLocal), NoEntity) ==
          static_cast<int>(DetachResult::NotAttached));
}

TEST_CASE("Skeletons.SpawnModel from C# spawns the model with its skeleton's SkeletonPose when the changes play back",
          "[skeleton][scripting][spawn]")
{
    SkeletonScriptingApp test;
    const auto model = test.assets.Store().Load<Model>(FENRIR_SKELETON_SCRIPTING_RIG, Active(test));
    REQUIRE(model.Ok());
    const auto root =
        test.Call<std::uint32_t>(AttachingType, "SpawnModel", model.handle.index, model.handle.generation);
    REQUIRE(root != NoEntity);
    REQUIRE(test.Entities().HasEntity(root));
    test.ApplyChanges();

    int ready = 0;
    std::function<void(const Entity&)> visit = [&](const Entity& entity) {
        if (entity.HasComponent<SkeletonPose>() && Skeletons::GetPoseState(Active(test), entity) == PoseState::Ready)
        {
            ++ready;
        }
        entity.ForEachChild(visit);
    };
    visit(test.Entities().GetEntity(root));
    CHECK(ready == 1);
}
