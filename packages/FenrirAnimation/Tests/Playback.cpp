// Playback through the Animator (ADR-0034): ADR-0034's interval table on a clip of duration 1, seeks and their
// precedence over clip changes, completion and its resets, and playback state. The slide clip moves the tip's x from
// 0 to 1, so its base-pose x is the playback cursor.
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

TEST_CASE("a step across a wrap crosses the end of the lap, then the start of the next, in order",
          "[animation][events]")
{
    AnimationApp test;
    const Entity skeleton =
        AnimationTest::Animated(test.level, AnimationTest::AddChain(test), AnimationTest::AddSlide(test, Quarters));
    EventReader<AnimationEvent> reader;
    test.RunFrame(0.75);
    (void)ReadTimes(test, reader);

    test.RunFrame(0.5);

    std::vector<std::uint64_t> laps;
    std::vector<float> times;
    for (const AnimationEvent& event : reader.Read(test.app))
    {
        times.push_back(event.time);
        laps.push_back(event.loop);
    }
    INFO("0.75 -> 1.25 crosses 1 in lap 0, then 0 and 0.25 in lap 1");
    REQUIRE(times == std::vector<float>{1.0f, 0.0f, 0.25f});
    REQUIRE(laps == std::vector<std::uint64_t>{0, 1, 1});
    INFO("the cursor wraps to 0.25 with one completed loop");
    const Fenrir::PlaybackInfo info = Animation::Playback(test.level, skeleton);
    REQUIRE(Near(info.time, 0.25));
    REQUIRE(info.completedLoops == 1);
    REQUIRE(Near(TipX(test, skeleton), 0.25f));
}

TEST_CASE("reverse playback crosses events in reverse order, wrapping across 0", "[animation][events]")
{
    AnimationApp test;
    const Entity skeleton = AnimationTest::Animated(test.level, AnimationTest::AddChain(test),
                                                    AnimationTest::AddSlide(test, Quarters), ClipLoop::Loop, -1.0f);
    EventReader<AnimationEvent> reader;

    test.RunFrame(0.75);
    INFO("starting in reverse at the duration crosses [0.25, 1], the event at 1 included, latest first");
    REQUIRE(ReadTimes(test, reader) == std::vector<float>{1.0f, 0.75f, 0.5f, 0.25f});

    test.RunFrame(0.5);
    INFO("0.25 -> -0.25 crosses 0, then wraps and crosses 1 and 0.75");
    REQUIRE(ReadTimes(test, reader) == std::vector<float>{0.0f, 1.0f, 0.75f});
    const Fenrir::PlaybackInfo info = Animation::Playback(test.level, skeleton);
    REQUIRE(Near(info.time, 0.75));
    REQUIRE(info.completedLoops == 1);
}

TEST_CASE("a seek crosses nothing, and playback from it crosses only what follows", "[animation][events][seek]")
{
    AnimationApp test;
    const Entity skeleton =
        AnimationTest::Animated(test.level, AnimationTest::AddChain(test), AnimationTest::AddSlide(test, Quarters));
    EventReader<AnimationEvent> reader;
    test.RunFrame(0.125);
    (void)ReadTimes(test, reader);

    Animation::Seek(test.level, skeleton, 0.25);
    test.RunFrame(0.25);

    INFO("the seek to 0.25 skips the event at 0.25 and its jump crosses nothing; the step crosses (0.25, 0.5]");
    REQUIRE(ReadTimes(test, reader) == std::vector<float>{0.5f});
    REQUIRE(Near(Animation::Playback(test.level, skeleton).time, 0.5));
}

TEST_CASE("a pause crosses nothing and holds the pose", "[animation][events]")
{
    AnimationApp test;
    const Entity skeleton =
        AnimationTest::Animated(test.level, AnimationTest::AddChain(test), AnimationTest::AddSlide(test, Quarters));
    EventReader<AnimationEvent> reader;
    test.RunFrame(0.5);
    (void)ReadTimes(test, reader);

    skeleton.GetComponent<Animator>().playing = false;
    test.RunFrame(0.5);
    test.RunFrame(0.5);

    INFO("paused at 0.5, nothing is crossed and the cursor and pose hold");
    REQUIRE(ReadTimes(test, reader).empty());
    REQUIRE(Near(Animation::Playback(test.level, skeleton).time, 0.5));
    REQUIRE(Near(TipX(test, skeleton), 0.5f));

    INFO("a speed of 0 holds just the same");
    skeleton.GetComponent<Animator>().playing = true;
    skeleton.GetComponent<Animator>().speed = 0.0f;
    test.RunFrame(0.5);
    REQUIRE(ReadTimes(test, reader).empty());
    REQUIRE(Near(Animation::Playback(test.level, skeleton).time, 0.5));
}

TEST_CASE("a zero-duration clip holds a stable pose, never wraps, and its events fire once", "[animation][events]")
{
    AnimationApp test;
    Fenrir::ClipData still;
    still.channels = {AnimationTest::Channel("tip", Fenrir::ClipPath::Translation, Fenrir::ClipInterpolation::Linear,
                                             {0.0f}, {{0.5f, 1.0f, 0.0f, 0.0f}})};
    still.events = {{0.0f, "pose"}};
    const auto clip = test.store.Add<Fenrir::Clip>(still, test.level);
    const Entity skeleton = AnimationTest::Animated(test.level, AnimationTest::AddChain(test), clip);
    EventReader<AnimationEvent> reader;

    std::vector<float> times;
    for (int frame = 0; frame < 4; ++frame)
    {
        test.RunFrame(0.5);
        for (const float time : ReadTimes(test, reader))
        {
            times.push_back(time);
        }
    }

    INFO("the event at 0 fired once, the cursor stays at 0 with no loops, and the pose is the first key's");
    REQUIRE(times == std::vector<float>{0.0f});
    const Fenrir::PlaybackInfo info = Animation::Playback(test.level, skeleton);
    REQUIRE(info.time == 0.0);
    REQUIRE(info.completedLoops == 0);
    REQUIRE(Near(TipX(test, skeleton), 0.5f));
}

TEST_CASE("a zero-duration Once clip finishes once, on its first evaluation", "[animation][completion]")
{
    AnimationApp test;
    Fenrir::ClipData still;
    still.channels = {AnimationTest::Channel("tip", Fenrir::ClipPath::Translation, Fenrir::ClipInterpolation::Step,
                                             {0.0f}, {{0.5f, 1.0f, 0.0f, 0.0f}})};
    const auto clip = test.store.Add<Fenrir::Clip>(still, test.level);
    const Entity skeleton = AnimationTest::Animated(test.level, AnimationTest::AddChain(test), clip, ClipLoop::Once);
    EventReader<Fenrir::ClipFinished> finished;

    test.RunFrame();
    INFO("finished at once, with one ClipFinished");
    REQUIRE(Animation::Playback(test.level, skeleton).finished);
    REQUIRE(CountFinished(test, finished) == 1);

    for (int frame = 0; frame < 2; ++frame)
    {
        test.RunFrame();
        INFO("and never again until restarted");
        REQUIRE(CountFinished(test, finished) == 0);
    }
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
    REQUIRE(Animation::Playback(test.level, skeleton).completedLoops == 100);
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

TEST_CASE("a seek is clamped by Once and wrapped by Loop into the clip", "[animation][seek]")
{
    AnimationApp test;
    const auto chain = AnimationTest::AddChain(test);
    const auto slide = AnimationTest::AddSlide(test);
    const Entity once = AnimationTest::Animated(test.level, chain, slide, ClipLoop::Once);
    const Entity loop = AnimationTest::Animated(test.level, chain, slide, ClipLoop::Loop);
    once.GetComponent<Animator>().playing = false;
    loop.GetComponent<Animator>().playing = false;

    Animation::Seek(test.level, once, 1.75);
    Animation::Seek(test.level, loop, 1.75);
    Animation::Seek(test.level, loop, -0.25);
    test.RunFrame();

    INFO("Once clamps 1.75 to 1; Loop wraps -0.25 to 0.75");
    REQUIRE(Near(Animation::Playback(test.level, once).time, 1.0));
    REQUIRE(Near(Animation::Playback(test.level, loop).time, 0.75));
}

TEST_CASE("a clip change restarts at 0, or at the duration in reverse", "[animation][playback]")
{
    AnimationApp test;
    const auto chain = AnimationTest::AddChain(test);
    const Entity forward = AnimationTest::Animated(test.level, chain, AnimationTest::AddSlide(test));
    const Entity reverse =
        AnimationTest::Animated(test.level, chain, AnimationTest::AddSlide(test), ClipLoop::Loop, -1.0f);
    test.RunFrame(0.5);

    const auto next = AnimationTest::AddSlide(test);
    forward.GetComponent<Animator>().clip = next;
    reverse.GetComponent<Animator>().clip = next;
    test.RunFrame(0.125);

    INFO("each restarts and takes this frame's step from there, with no loops counted");
    REQUIRE(Near(Animation::Playback(test.level, forward).time, 0.125));
    REQUIRE(Near(Animation::Playback(test.level, reverse).time, 0.875));
    REQUIRE(Animation::Playback(test.level, reverse).completedLoops == 0);
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

TEST_CASE("a Once clip finishes once at its end, and pausing keeps it finished", "[animation][completion]")
{
    AnimationApp test;
    const Entity skeleton = AnimationTest::Animated(test.level, AnimationTest::AddChain(test),
                                                    AnimationTest::AddSlide(test, Quarters), ClipLoop::Once);
    EventReader<Fenrir::ClipFinished> finished;
    EventReader<AnimationEvent> events;

    std::size_t finishes = 0;
    std::vector<float> times;
    for (int frame = 0; frame < 3; ++frame)
    {
        test.RunFrame(0.75);
        REQUIRE(Animation::Playback(test.level, skeleton).finished == (frame > 0));
        finishes += CountFinished(test, finished);
        for (const float time : ReadTimes(test, events))
        {
            times.push_back(time);
        }
    }

    INFO("it clamps at 1 and holds, finished, with one ClipFinished; its boundary event is crossed once");
    const Fenrir::PlaybackInfo info = Animation::Playback(test.level, skeleton);
    REQUIRE(Near(info.time, 1.0));
    REQUIRE(Near(TipX(test, skeleton), 1.0f));
    REQUIRE(finishes == 1);
    REQUIRE(times == std::vector<float>{0.0f, 0.25f, 0.5f, 0.75f, 1.0f});

    skeleton.GetComponent<Animator>().playing = false;
    test.RunFrame();
    INFO("pausing keeps it finished");
    REQUIRE(Animation::Playback(test.level, skeleton).finished);
}

TEST_CASE("finished resets on a clip change, a seek and a reversal away from the boundary", "[animation][completion]")
{
    AnimationApp test;
    const Entity skeleton = AnimationTest::Animated(test.level, AnimationTest::AddChain(test),
                                                    AnimationTest::AddSlide(test), ClipLoop::Once);
    EventReader<Fenrir::ClipFinished> finished;
    test.RunFrame(2.0);
    REQUIRE(CountFinished(test, finished) == 1);

    Animation::Seek(test.level, skeleton, 0.5);
    test.RunFrame(0.125);
    INFO("a seek resets it, and reaching the end again finishes again");
    REQUIRE(!Animation::Playback(test.level, skeleton).finished);
    test.RunFrame(2.0);
    REQUIRE(CountFinished(test, finished) == 1);

    skeleton.GetComponent<Animator>().speed = -1.0f;
    test.RunFrame(0.125);
    INFO("reversing away from the end resets it; reaching 0 finishes again");
    REQUIRE(!Animation::Playback(test.level, skeleton).finished);
    test.RunFrame(2.0);
    REQUIRE(Animation::Playback(test.level, skeleton).finished);
    REQUIRE(Near(Animation::Playback(test.level, skeleton).time, 0.0));
    REQUIRE(CountFinished(test, finished) == 1);

    INFO("speeding up towards the boundary it holds at keeps it finished, with no second event");
    skeleton.GetComponent<Animator>().speed = -2.0f;
    test.RunFrame(0.125);
    REQUIRE(Animation::Playback(test.level, skeleton).finished);
    REQUIRE(CountFinished(test, finished) == 0);

    skeleton.GetComponent<Animator>().clip = AnimationTest::AddSlide(test);
    test.RunFrame(0.125);
    INFO("a clip change resets it");
    REQUIRE(!Animation::Playback(test.level, skeleton).finished);
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
