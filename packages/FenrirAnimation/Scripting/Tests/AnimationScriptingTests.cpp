// Animation from C# (spec seam 7), through the fixture's Animating and Recorded classes: ClipEventId.Of matching C++,
// clip events authored and read by handle, events read in C# through EventReader, playback started, sought and read
// from C# agreeing with C++, and bone masks made and edited. The model is the animation tests' fixture, rig.gltf: one
// skeleton (Shoulder, Elbow) and two clips.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "AnimationScriptingApp.hpp"
#include "FenrirAnimation/Animator.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/BoneMaskData.hpp"
#include "FenrirAssets/ClipData.hpp"
#include "FenrirAssets/Model.hpp"
#include "FenrirSkeleton/SkeletonPose.hpp"

using namespace Fenrir;
using namespace Fenrir::Testing;

namespace
{
    constexpr const char* AnimatingType = "Fenrir.AnimationScriptFixture.Animating";
    constexpr const char* RecordedType = "Fenrir.AnimationScriptFixture.Recorded";

    // The fixture model spawned into the active scene: its skeleton entity and its first clip.
    struct SpawnedFixture
    {
        Entity skeleton;
        AssetHandle<Clip> clip;
    };

    SpawnedFixture SpawnFixture(AnimationScriptingApp& test)
    {
        AssetStore& store = test.assets.Store();
        Scene& scene = test.Get().GetActiveScene();
        const auto model = store.Load<Model>(FENRIR_ANIMATION_SCRIPTING_RIG, scene);
        REQUIRE(model.Ok());
        const SpawnedNodes spawned = test.skeletons.SpawnModel(scene, model.handle);
        SpawnedFixture fixture;
        for (const Entity& node : spawned.nodes)
        {
            if (node.IsValid() && node.HasComponent<SkeletonPose>())
            {
                fixture.skeleton = node;
            }
        }
        REQUIRE(fixture.skeleton.IsValid());
        const Model* data = store.TryGet(model.handle);
        REQUIRE(data != nullptr);
        REQUIRE_FALSE(data->clips.empty());
        fixture.clip = data->clips[0];
        return fixture;
    }
} // namespace

TEST_CASE("ClipEventId.Of in C# matches C++'s FNV-1a", "[animation][scripting][events]")
{
    AnimationScriptingApp test;
    for (const auto& [index, name] : {std::pair{0, std::string_view("step")}, std::pair{1, std::string_view("")},
                                      std::pair{2, std::string_view("Footstep")},
                                      std::pair{3, std::string_view("\xC3\xBCn\xC3\xAF"
                                                                    "c\xC3\xB6"
                                                                    "d\xC3\xA9")}})
    {
        INFO("Name " << index);
        CHECK(test.Call<std::uint32_t>(AnimatingType, "IdOf", index) == ClipEventId::Of(name).value);
    }
}

TEST_CASE("Clip events authored from C# are the clip's, and C# reads them back by handle", "[animation][scripting]")
{
    AnimationScriptingApp test;
    const SpawnedFixture fixture = SpawnFixture(test);
    AssetStore& store = test.assets.Store();
    const std::uint32_t index = fixture.clip.index;
    const std::uint32_t generation = fixture.clip.generation;
    const float duration = store.TryGet(fixture.clip)->duration;

    CHECK(test.Call<float>(AnimatingType, "Duration", index, generation) == Catch::Approx(duration));
    const int before = test.Call<int>(AnimatingType, "EventCount", index, generation);
    const auto added = test.Call<std::uint32_t>(AnimatingType, "AddEvent", index, generation, duration * 0.5f, 0);
    REQUIRE(added != 0xFFFFFFFFu);
    CHECK(test.Call<int>(AnimatingType, "EventCount", index, generation) == before + 1);
    REQUIRE(store.TryGet(fixture.clip)->events.size() == static_cast<std::size_t>(before + 1));
    CHECK(store.TryGet(fixture.clip)->events[added].name == "step");
    CHECK(test.Call<float>(AnimatingType, "EventTime", index, generation, added) == Catch::Approx(duration * 0.5f));
    CHECK(test.Call<std::optional<std::string>>(AnimatingType, "EventName", index, generation, added) ==
          std::optional<std::string>("step"));
    CHECK(test.Call<int>(AnimatingType, "FindEvents", index, generation, 0) == 1);
    INFO("A time past the clip's end is refused");
    CHECK(test.Call<std::uint32_t>(AnimatingType, "AddEvent", index, generation, duration + 1.0f, 2) == 0xFFFFFFFFu);
    CHECK(test.Call<bool>(AnimatingType, "RemoveEvent", index, generation, added));
    CHECK(test.Call<int>(AnimatingType, "EventCount", index, generation) == before);
}

TEST_CASE("Playback started from C# plays, its events are read in C#, and Seek and Playback agree with C++",
          "[animation][scripting][playback]")
{
    AnimationScriptingApp test;
    const SpawnedFixture fixture = SpawnFixture(test);
    AssetStore& store = test.assets.Store();
    const float duration = store.TryGet(fixture.clip)->duration;
    REQUIRE(store.AddClipEvent(fixture.clip, duration * 0.25f, "step").has_value());

    test.Call<void>(AnimatingType, "Play", fixture.skeleton.GetId(), fixture.clip.index, fixture.clip.generation, 0.0f);
    test.ApplyChanges();
    REQUIRE(fixture.skeleton.HasComponent<Animator>());
    const int frames = static_cast<int>(duration * 0.5f / oneTick) + 2;
    test.Run(frames);

    CHECK(test.Call<int>(AnimatingType, "PlaybackState", fixture.skeleton.GetId()) ==
          static_cast<int>(PoseState::Ready));
    CHECK(test.Call<double>(AnimatingType, "PlaybackTime", fixture.skeleton.GetId()) ==
          Catch::Approx(Animation::Playback(test.Get().GetActiveScene(), fixture.skeleton).time));
    REQUIRE(test.Call<int>(RecordedType, "EventCount") >= 1);
    CHECK(test.Call<std::uint32_t>(RecordedType, "EventNameId", 0) == ClipEventId::Of("step").value);
    CHECK(test.Call<std::uint32_t>(RecordedType, "EventSkeleton", 0) == fixture.skeleton.GetId());
    CHECK(test.Call<float>(RecordedType, "EventTime", 0) == Catch::Approx(duration * 0.25f));

    CHECK(test.Call<bool>(AnimatingType, "Seek", fixture.skeleton.GetId(), static_cast<double>(duration) * 0.1));
    test.Frame();
    const double time = Animation::Playback(test.Get().GetActiveScene(), fixture.skeleton).time;
    INFO("A seek moves the cursor at the next evaluation, which then advances by the frame");
    CHECK(time == Catch::Approx(duration * 0.1 + oneTick).margin(1e-4));
    CHECK(test.Call<double>(AnimatingType, "PlaybackTime", fixture.skeleton.GetId()) == Catch::Approx(time));
}

TEST_CASE("Bone masks made and edited from C# are the store's", "[animation][scripting][masks]")
{
    AnimationScriptingApp test;
    const auto packed = test.Call<std::uint64_t>(AnimatingType, "MakeMask");
    const AssetHandle<BoneMask> mask{static_cast<std::uint32_t>(packed >> 32), static_cast<std::uint32_t>(packed)};
    const BoneMaskData* data = test.assets.Store().TryGet(mask);
    REQUIRE(data != nullptr);
    REQUIRE(data->entries.size() == 1);
    CHECK(data->entries[0].boneName == "Elbow");
    CHECK(data->entries[0].weight == Catch::Approx(0.5f));
    CHECK(data->entries[0].scope == MaskScope::Subtree);
    CHECK(test.Call<bool>(AnimatingType, "ClearMask", mask.index, mask.generation));
    CHECK(test.assets.Store().TryGet(mask)->entries.empty());
}
