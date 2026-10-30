// The animation natives' rules, with no .NET and no fixture assembly: through FenrirScripting's NativeCallHarness,
// which runs the animation extension as shipped (Animation::ScriptingExtension()), each native is called from C++ by
// its registered name through the registration's own thunk. The rules: with no asset store, a stale handle, an event
// index out of range or an owner that is neither, a native does nothing and returns false, 0 or nothing; with
// FenrirAnimation not installed nothing plays; and each warns once per native and message, under the native's name, in
// the words AnimationScripting (or Assets' AssetResolve) names. What a script sees of animation stays in
// AnimationScriptingTests.cpp, on .NET.
#include <catch2/catch_test_macros.hpp>

#include "NativeCallHarness.hpp"

#include <cstdint>
#include <limits>
#include <string_view>

#include "AnimationScriptingState.hpp"
#include "FenrirAnimation/Animation.hpp"
#include "FenrirAnimationScripting/AnimationScripting.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirAssets/ClipData.hpp"
#include "FenrirAssetsScripting/AssetResolve.hpp"
#include "FenrirAssetsScripting/ScriptAssets.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirSkeleton/Skeletons.hpp"

using namespace std::string_view_literals;

namespace
{
    using namespace Fenrir;

    // A clip of one channel keyed at 0 and 1 second, so its duration is 1.
    AssetHandle<Clip> AddClip(Testing::NativeCallHarness& harness, Assets& assets)
    {
        ClipData clip;
        ClipChannel channel;
        channel.targetName = "root";
        channel.times = {0.0f, 1.0f};
        channel.values = {Math::Vec4(0.0f), Math::Vec4(1.0f)};
        clip.channels.push_back(channel);
        return assets.Store().Add<Clip>(std::move(clip), harness.GetApp().GetActiveScene());
    }
} // namespace

TEST_CASE("With no asset store or FenrirAnimation, an animation native does nothing and warns once under its name",
          "[animation][scripting][rules]")
{
    Testing::NativeCallHarness harness({Animation::ScriptingExtension()});
    const AssetHandle<Clip> clip;
    for (int call = 0; call < 2; ++call)
    {
        CHECK(harness.Call<float>("AnimationNative.Duration", &clip) == 0.0f);
    }
    CHECK(harness.WarnedOnce("AnimationNative.Duration", AssetResolve::NoStore));

    AssetHandle<BoneMask> mask{3, 3};
    CHECK_FALSE(harness.Call<bool>("AnimationNative.Create", ScriptAssetOwner::Scene, &mask));
    CHECK(mask.IsEmpty());
    CHECK(harness.WarnedOnce("AnimationNative.Create", AssetResolve::NoStore));

    const Entity entity = harness.GetApp().GetActiveScene().GetEntityList().CreateEntity();
    PlaybackInfo info;
    info.time = 4.0;
    CHECK_FALSE(harness.Call<bool>("AnimationNative.Playback", &entity, &info));
    CHECK(info.state == PoseState::Missing);
    CHECK(info.time == 0.0);
    CHECK(harness.Call<bool>("AnimationNative.Seek", &entity, 1.0));
}

TEST_CASE("A stale clip or mask, an event out of range, a refused edit and a bad owner each warn once",
          "[animation][scripting][rules]")
{
    Testing::NativeCallHarness harness({Animation::ScriptingExtension()});
    Assets assets = Assets::Install(harness.GetApp());
    const AssetHandle<Clip> clip = AddClip(harness, assets);
    REQUIRE_FALSE(clip.IsEmpty());

    const AssetHandle<Clip> stale{clip.index + 9, 1};
    CHECK(harness.Call<std::int32_t>("AnimationNative.EventCount", &stale) == 0);
    CHECK(harness.WarnedOnce("AnimationNative.EventCount", AssetResolve::StaleHandle("clip")));

    float time = 5.0f;
    CHECK_FALSE(harness.Call<bool>("AnimationNative.GetEvent", &clip, std::uint32_t{0}, &time));
    CHECK(time == 0.0f);
    CHECK(harness.WarnedOnce("AnimationNative.GetEvent", AnimationScripting::NoSuchEvent));
    CHECK_FALSE(harness.Call<bool>("AnimationNative.RemoveEvent", &clip, std::uint32_t{0}));
    CHECK(harness.WarnedOnce("AnimationNative.RemoveEvent", AnimationScripting::NoSuchEvent));

    std::uint32_t index = 9;
    CHECK_FALSE(harness.Call<bool>("AnimationNative.AddEvent", &clip, 3.0f, "late"sv, &index));
    CHECK(harness.WarnedOnce("AnimationNative.AddEvent", AnimationScripting::Refused));
    CHECK(harness.Call<bool>("AnimationNative.AddEvent", &clip, 0.5f, "half"sv, &index));
    CHECK(index == 0);

    AssetHandle<BoneMask> mask;
    CHECK_FALSE(harness.Call<bool>("AnimationNative.Create", static_cast<ScriptAssetOwner>(7), &mask));
    CHECK(harness.WarnedOnce("AnimationNative.Create", AnimationScripting::NoOwner));
    REQUIRE(harness.Call<bool>("AnimationNative.Create", ScriptAssetOwner::App, &mask));
    CHECK_FALSE(harness.Call<bool>("AnimationNative.Include", static_cast<const AssetHandle<BoneMask>*>(&mask),
                                   "root"sv, std::numeric_limits<float>::infinity(), MaskScope::Bone));
    CHECK(harness.WarnedOnce("AnimationNative.Include", AnimationScripting::Refused));
    const AssetHandle<BoneMask> staleMask{mask.index + 9, 1};
    CHECK_FALSE(harness.Call<bool>("AnimationNative.Clear", &staleMask));
    CHECK(harness.WarnedOnce("AnimationNative.Clear", AssetResolve::StaleHandle("bone mask")));
}
