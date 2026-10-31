// Playback through the Animator (ADR-0034): the wiring around the playback cursor, whose interval table is
// PlaybackCursor.cpp's. Events and their budget as sent, seeks and their precedence over clip changes, a clip change's
// events, and playback state. The slide clip moves the tip's x from 0 to 1, so its base-pose x is the playback cursor.
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "AnimationTest.hpp"

using AnimationTest::AnimationApp;
using AnimationTest::Near;
using AnimationTest::ReadTimes;
using AnimationTest::TipX;
using Fenrir::Animation;
using Fenrir::AnimationEvent;
using Fenrir::Animator;
using Fenrir::ClipLoop;
using Fenrir::Entity;
using Fenrir::EventReader;

namespace
{
    const std::vector<float> Quarters{0.0f, 0.25f, 0.5f, 0.75f, 1.0f};

    std::size_t CountFinished(AnimationApp& test, EventReader<Fenrir::ClipFinished>& reader)
    {
        std::size_t count = 0;
        for (const Fenrir::ClipFinished& finished : reader.Read(test.app))
        {
            (void)finished;
            ++count;
        }
        return count;
    }
} // namespace

TEST_CASE("forward playback from 0 crosses the events up to the cursor, the one at 0 included", "[animation][events]")
{
    AnimationApp test;
    const Entity skeleton =
        AnimationTest::Animated(test.level, AnimationTest::AddChain(test), AnimationTest::AddSlide(test, Quarters));
    EventReader<AnimationEvent> reader;

    test.RunFrame(0.5);

    INFO("0 -> 0.5 crosses [0, 0.5], in order, and samples the clip at 0.5");
    REQUIRE(ReadTimes(test, reader) == std::vector<float>{0.0f, 0.25f, 0.5f});
    REQUIRE(Near(Animation::Playback(test.level, skeleton).time, 0.5));
    REQUIRE(Near(TipX(test, skeleton), 0.5f));

    test.RunFrame(0.25);
    INFO("the next step crosses (0.5, 0.75]: the event at 0.5 is not crossed twice");
    REQUIRE(ReadTimes(test, reader) == std::vector<float>{0.75f});
}

TEST_CASE("a step across 100 laps counts them exactly, sends the budget's earliest events and reports the rest",
          "[animation][events][budget]")
{
    AnimationApp test;
    const Entity skeleton = AnimationTest::Animated(test.level, AnimationTest::AddChain(test),
                                                    AnimationTest::AddSlide(test, {0.1f, 0.3f, 0.5f, 0.7f, 0.9f}));
    EventReader<AnimationEvent> events;
    EventReader<Fenrir::AnimationEventsDropped> drops;

    test.RunFrame(100.0);

    std::vector<AnimationEvent> sent;
    for (const AnimationEvent& event : events.Read(test.app))
    {
        sent.push_back(event);
    }
    std::vector<std::uint64_t> dropped;
    for (const Fenrir::AnimationEventsDropped& drop : drops.Read(test.app))
    {
        dropped.push_back(drop.dropped);
        REQUIRE(drop.skeleton == skeleton);
    }
    INFO("100 laps of 5 events is 500 crossings: the first 64 are sent, and 436 counted as dropped");
    REQUIRE(sent.size() == 64);
    REQUIRE(Near(sent.front().time, 0.1f));
    REQUIRE(sent.front().loop == 0);
    // The 64th crossing is lap 12's fourth event (12 laps of 5, then 4 more).
    REQUIRE(sent.back().index == 3);
    REQUIRE(sent.back().loop == 12);
    REQUIRE(dropped == std::vector<std::uint64_t>{436});
    INFO("the drop is logged once");
    REQUIRE(test.Warnings("more clip events in one frame than its budget") == 1);

    test.RunFrame(100.0);
    INFO("another flood is reported again, but logged no more for this skeleton");
    std::size_t reported = 0;
    for (const Fenrir::AnimationEventsDropped& drop : drops.Read(test.app))
    {
        reported += drop.dropped == 436 ? 1 : 0;
    }
    REQUIRE(reported == 1);
    REQUIRE(test.Warnings("more clip events in one frame than its budget") == 1);
}

TEST_CASE("the event budget is the configured one", "[animation][events][budget]")
{
    Fenrir::AnimationConfig config;
    config.maxEventsPerInstancePerFrame = 2;
    AnimationApp test(config);
    AnimationTest::Animated(test.level, AnimationTest::AddChain(test), AnimationTest::AddSlide(test, Quarters));
    EventReader<AnimationEvent> reader;

    test.RunFrame(0.75);

    INFO("only the earliest two of the four crossings are sent");
    REQUIRE(ReadTimes(test, reader) == std::vector<float>{0.0f, 0.25f});
}

TEST_CASE("a clip change and a seek before one evaluation start the new clip at the seek, in either order",
          "[animation][seek]")
{
    for (const bool seekFirst : {false, true})
    {
        AnimationApp test;
        const auto chain = AnimationTest::AddChain(test);
        const Entity skeleton = AnimationTest::Animated(test.level, chain, AnimationTest::AddSlide(test));
        const auto run = AnimationTest::AddSlide(test);
        test.RunFrame();
        skeleton.GetComponent<Animator>().playing = false;

        if (seekFirst)
        {
            Animation::Seek(test.level, skeleton, 0.5);
            skeleton.GetComponent<Animator>().clip = run;
        }
        else
        {
            skeleton.GetComponent<Animator>().clip = run;
            Animation::Seek(test.level, skeleton, 0.5);
        }
        test.RunFrame();

        INFO("the new clip starts at 0.5, not at 0, whichever was written first (seek first: " << seekFirst << ")");
        REQUIRE(Near(Animation::Playback(test.level, skeleton).time, 0.5));
        REQUIRE(Near(TipX(test, skeleton), 0.5f));
    }
}

TEST_CASE("the latest pending seek wins, and a seek while paused moves the pose without resuming", "[animation][seek]")
{
    AnimationApp test;
    const Entity skeleton =
        AnimationTest::Animated(test.level, AnimationTest::AddChain(test), AnimationTest::AddSlide(test));
    test.RunFrame();
    skeleton.GetComponent<Animator>().playing = false;

    Animation::Seek(test.level, skeleton, 0.25);
    Animation::Seek(test.level, skeleton, 0.75);
    test.RunFrame();
    test.RunFrame();

    INFO("the cursor and pose are at 0.75 and stay there");
    REQUIRE(Near(Animation::Playback(test.level, skeleton).time, 0.75));
    REQUIRE(Near(TipX(test, skeleton), 0.75f));
}

TEST_CASE("a seek may come before the entity has an Animator or a skeleton instance", "[animation][seek]")
{
    AnimationApp test;
    Entity skeleton = test.level.GetEntityList().CreateEntity();
    Animation::Seek(test.level, skeleton, 0.5);
    test.RunFrame();

    skeleton.AddComponent<Fenrir::SkeletonPose>(Fenrir::SkeletonPose{AnimationTest::AddChain(test)});
    Animator animator;
    animator.clip = AnimationTest::AddSlide(test);
    animator.playing = false;
    skeleton.AddComponent<Animator>(animator);
    test.RunFrame();

    INFO("the first evaluation applies it");
    REQUIRE(Near(Animation::Playback(test.level, skeleton).time, 0.5));
    REQUIRE(Near(TipX(test, skeleton), 0.5f));
}

TEST_CASE("a clip change crosses nothing, and the new clip's start crosses its starting boundary",
          "[animation][events]")
{
    AnimationApp test;
    const Entity skeleton =
        AnimationTest::Animated(test.level, AnimationTest::AddChain(test), AnimationTest::AddSlide(test, Quarters));
    EventReader<AnimationEvent> reader;
    test.RunFrame(0.625);
    (void)ReadTimes(test, reader);

    const auto next = AnimationTest::AddSlide(test, {0.0f, 0.6f});
    skeleton.GetComponent<Animator>().clip = next;
    test.RunFrame(0.125);

    std::vector<Fenrir::AssetHandle<Fenrir::Clip>> clips;
    std::vector<float> times;
    for (const AnimationEvent& event : reader.Read(test.app))
    {
        clips.push_back(event.clip);
        times.push_back(event.time);
    }
    INFO("only the new clip's event at 0: nothing of the old clip, nothing for the jump");
    REQUIRE(times == std::vector<float>{0.0f});
    REQUIRE(clips == std::vector{next});
}

TEST_CASE("Playback reads Missing without an Animator, and Unavailable on a skeleton that is not Ready",
          "[animation][playback]")
{
    AnimationApp test;
    const Entity plain = test.level.GetEntityList().CreateEntity();
    Entity broken = test.level.GetEntityList().CreateEntity();
    broken.AddComponent<Fenrir::SkeletonPose>();
    Animator animator;
    animator.clip = AnimationTest::AddSlide(test);
    broken.AddComponent<Animator>(animator);
    test.RunFrame();

    INFO("no Animator: Missing; an empty skeleton handle: Unavailable, not advanced");
    REQUIRE(Animation::Playback(test.level, plain).state == Fenrir::PoseState::Missing);
    const Fenrir::PlaybackInfo info = Animation::Playback(test.level, broken);
    REQUIRE(info.state == Fenrir::PoseState::Unavailable);
    REQUIRE(info.time == 0.0);
    REQUIRE(!info.fading);
    REQUIRE(info.fadeProgress == 0.0f);
}

TEST_CASE("with no Animator or an empty clip the base pose is left alone", "[animation][playback]")
{
    AnimationApp test;
    const auto chain = AnimationTest::AddChain(test);
    Entity posed = test.level.GetEntityList().CreateEntity();
    posed.AddComponent<Fenrir::SkeletonPose>(Fenrir::SkeletonPose{chain});
    const Entity empty = AnimationTest::Animated(test.level, chain, {});
    test.RunFrame();

    Fenrir::Skeletons::EditBasePose(test.level, posed)[2].pos.x = 0.75f;
    Fenrir::Skeletons::EditBasePose(test.level, empty)[2].pos.x = 0.25f;
    test.RunFrame();
    test.RunFrame();

    INFO("hand-posed values persist");
    REQUIRE(Near(TipX(test, posed), 0.75f));
    REQUIRE(Near(TipX(test, empty), 0.25f));
}

TEST_CASE("while the Animator names a clip, bones and channels the clip lacks return to rest each frame",
          "[animation][playback]")
{
    AnimationApp test;
    const Entity skeleton =
        AnimationTest::Animated(test.level, AnimationTest::AddChain(test), AnimationTest::AddSlide(test));
    test.RunFrame();

    // A hand edit before the pipeline: the sampler overwrites the whole base pose from rest.
    std::span<Fenrir::Transform> base = Fenrir::Skeletons::EditBasePose(test.level, skeleton);
    base[1].pos = {3.0f, 3.0f, 3.0f};
    base[2].pos.y = 7.0f;
    base[2].scale = {2.0f, 2.0f, 2.0f};
    test.RunFrame();

    INFO("mid is back at rest; the tip's unkeyed scale is rest, and its keyed translation the clip's");
    const auto mid = Fenrir::Skeletons::GetBaseLocal(test.level, skeleton, 1);
    const auto tip = Fenrir::Skeletons::GetBaseLocal(test.level, skeleton, 2);
    REQUIRE(Near(mid->pos, {0.0f, 1.0f, 0.0f}));
    REQUIRE(Near(tip->scale, {1.0f, 1.0f, 1.0f}));
    REQUIRE(Near(tip->pos, {0.25f, 1.0f, 0.0f}));
}
