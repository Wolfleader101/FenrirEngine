// The skeleton natives' rules, with no .NET and no fixture assembly: through FenrirScripting's NativeCallHarness, which
// runs the skeleton extension as shipped (Skeletons::ScriptingExtension()), each native is called from C++ by its
// registered name through the registration's own thunk. The rules: with FenrirSkeleton (or the asset store) missing a
// native does nothing; on an entity that is not a Ready skeleton entity, or a bone out of range, it reads and writes
// nothing; the stage natives do nothing outside their stage, and a stale pose edit writes nothing; and each warns once
// per native and message, under the native's name, in the words SkeletonScripting names. What a script sees of
// skeletons stays in SkeletonScriptingTests.cpp, on .NET.
#include <catch2/catch_test_macros.hpp>

#include "NativeCallHarness.hpp"

#include <cstdint>
#include <string_view>

#include "FenrirAssets/Assets.hpp"
#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirAssetsScripting/AssetResolve.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirScripting/NativeGuard.hpp"
#include "FenrirSkeleton/BoneAttachment.hpp"
#include "FenrirSkeleton/PoseStages.hpp"
#include "FenrirSkeleton/SkeletonPose.hpp"
#include "FenrirSkeleton/Skeletons.hpp"
#include "FenrirSkeletonScripting/SkeletonScripting.hpp"
#include "SkeletonScriptingState.hpp"

using namespace std::string_view_literals;

namespace
{
    using namespace Fenrir;

    EntityList& Entities(Testing::NativeCallHarness& harness)
    {
        return harness.GetApp().GetActiveScene().GetEntityList();
    }

    // A one-bone skeleton entity, made Ready at once.
    Entity SpawnBone(Testing::NativeCallHarness& harness, Assets& assets, Skeletons& skeletons)
    {
        SkeletonData data;
        data.bones.resize(1);
        const AssetHandle<Skeleton> skeleton =
            assets.Store().Add<Skeleton>(std::move(data), harness.GetApp().GetActiveScene());
        Entity entity = Entities(harness).CreateEntity();
        entity.AddComponent<SkeletonPose>(SkeletonPose{skeleton});
        REQUIRE(skeletons.Ensure(harness.GetApp().GetActiveScene(), entity) == PoseState::Ready);
        return entity;
    }

    constexpr std::int32_t Root = 0;
} // namespace

TEST_CASE("With FenrirSkeleton not installed, a skeleton native reads and writes nothing and warns once under its name",
          "[skeleton][scripting][rules]")
{
    Testing::NativeCallHarness harness({Skeletons::ScriptingExtension()});
    const Entity entity = Entities(harness).CreateEntity();

    for (int call = 0; call < 2; ++call)
    {
        Transform local;
        INFO("A bone read with no skeleton should return false, not throw");
        REQUIRE_FALSE(harness.Call<bool>("SkeletonNative.GetLocal", &entity, Root, &local));
    }
    CHECK(harness.WarnedOnce("SkeletonNative.GetLocal", SkeletonScripting::NotReady));

    Transform locals[2];
    CHECK(harness.Call<std::int32_t>("SkeletonNative.ReadLocals", &entity, &locals[0], std::int32_t{2}) == 0);
    CHECK(harness.Call<std::int32_t>("SkeletonNative.FindBone", &entity, "root"sv) == NoBone);
    CHECK(harness.Call<std::uint64_t>("SkeletonNative.GetPoseGeneration", &entity) == 0);
    const Transform written;
    CHECK_FALSE(harness.Call<bool>("SkeletonNative.SetBaseLocal", &entity, Root, &written));
    CHECK(harness.WarnedOnce("SkeletonNative.SetBaseLocal", SkeletonScripting::NotReady));

    INFO("A skeleton asset query with no asset store warns in Assets' words");
    const AssetHandle<Skeleton> none;
    CHECK(harness.Call<std::int32_t>("SkeletonNative.BoneCount", &none) == 0);
    CHECK(harness.WarnedOnce("SkeletonNative.BoneCount", AssetResolve::NoStore));
}

TEST_CASE("The stage natives do nothing outside their stage, and a stale pose edit writes nothing",
          "[skeleton][scripting][rules]")
{
    Testing::NativeCallHarness harness({Skeletons::ScriptingExtension()});
    const Entity entity = Entities(harness).CreateEntity();

    CHECK(harness.Call<double>("SkeletonNative.GetStep") == 0.0);
    CHECK(harness.WarnedOnce("SkeletonNative.GetStep", SkeletonScripting::OutsideAnimate));
    (void)harness.Call<PoseTiming>("SkeletonNative.GetTiming");
    CHECK(harness.WarnedOnce("SkeletonNative.GetTiming", SkeletonScripting::OutsideAnimate));

    std::uint64_t token = 7;
    CHECK_FALSE(harness.Call<bool>("SkeletonNative.EditOpen", &entity, &token));
    CHECK(token == 0);
    CHECK(harness.WarnedOnce("SkeletonNative.EditOpen", SkeletonScripting::OutsideModify));

    const Target target;
    CHECK_FALSE(harness.Call<bool>("SkeletonNative.EditWrite", std::uint64_t{1}, &entity, Root, &target));
    CHECK(harness.WarnedOnce("SkeletonNative.EditWrite", SkeletonScripting::StaleEdit));
}

TEST_CASE("On a Ready skeleton, a bone out of range, a refused base write and a bad detach each warn once",
          "[skeleton][scripting][rules]")
{
    Testing::NativeCallHarness harness({Skeletons::ScriptingExtension()});
    Assets assets = Assets::Install(harness.GetApp());
    Skeletons skeletons = Skeletons::Install(harness.GetApp());
    const Entity skeleton = SpawnBone(harness, assets, skeletons);

    Transform local;
    CHECK(harness.Call<bool>("SkeletonNative.GetLocal", &skeleton, Root, &local));
    CHECK_FALSE(harness.Call<bool>("SkeletonNative.GetLocal", &skeleton, std::int32_t{4}, &local));
    CHECK(harness.WarnedOnce("SkeletonNative.GetLocal", SkeletonScripting::NoSuchBone));

    const AssetHandle<Skeleton> stale{99, 1};
    CHECK(harness.Call<std::int32_t>("SkeletonNative.BoneCount", &stale) == 0);
    CHECK(harness.WarnedOnce("SkeletonNative.BoneCount", AssetResolve::StaleHandle("skeleton")));

    // A base-pose write from inside Modify, where FenrirSkeleton refuses it.
    bool wrote = true;
    (void)skeletons.AddModifier("writes base", 0, [&](ModifierContext&) {
        const Transform base;
        wrote = harness.Call<bool>("SkeletonNative.SetBaseLocal", &skeleton, Root, &base);
    });
    harness.GetApp().RunFrame(1.0 / 60.0);
    CHECK_FALSE(wrote);
    CHECK(harness.WarnedOnce("SkeletonNative.SetBaseLocal", SkeletonScripting::BaseWriteRefused));

    Entity loose = Entities(harness).CreateEntity();
    const Entity none;
    CHECK(harness.Call<DetachResult>("SkeletonNative.Detach", static_cast<const Entity*>(&loose), DetachMode::KeepLocal,
                                     &none) == DetachResult::NotAttached);
    CHECK(harness.WarnedOnce("SkeletonNative.Detach", SkeletonScripting::NotAttached));

    loose.AddComponent<BoneAttachment>(BoneAttachment{skeleton});
    Entity gone = Entities(harness).CreateEntity();
    const Entity goneHandle = gone;
    Entities(harness).DestroyEntity(gone);
    CHECK(harness.Call<DetachResult>("SkeletonNative.Detach", static_cast<const Entity*>(&loose), DetachMode::KeepWorld,
                                     &goneHandle) == DetachResult::InvalidParent);
    CHECK(harness.WarnedOnce("SkeletonNative.Detach", SkeletonScripting::DeadParent));
}
