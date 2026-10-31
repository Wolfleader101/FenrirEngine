// The playback cursor (ADR-0034), through its private header: the agreed test seam for the interval table, with no
// App, scene or assets. Every row is a clip of duration 1; what a step crossed is read as (event time, lap) pairs in
// playback order. The App tests in Playback.cpp keep the wiring: events and ClipFinished sent, seeks applied, the
// budget shared.
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirAssets/ClipData.hpp"
#include "PlaybackCursor.hpp"

using Fenrir::ClipLoop;
using Fenrir::Detail::Crossing;
using Fenrir::Detail::CursorStep;
using Fenrir::Detail::PlaybackCursor;

namespace
{
    using Crossed = std::vector<std::pair<float, std::uint64_t>>;

    // A clip of duration `duration` with an event at each of `times`, in order.
    Fenrir::ClipData Clip(const std::vector<float>& times, float duration = 1.0f)
    {
        Fenrir::ClipData clip;
        clip.duration = duration;
        for (const float time : times)
        {
            clip.events.push_back({time, "e"});
        }
        return clip;
    }

    const std::vector<float> Quarters{0.0f, 0.25f, 0.5f, 0.75f, 1.0f};

    // Every crossing of the step, as (event time, lap).
    Crossed Read(const Fenrir::ClipData& clip, const CursorStep& step)
    {
        Crossed crossed;
        if (!step.crossings)
        {
            return crossed;
        }
        std::vector<Crossing> all;
        step.crossings->Earliest(clip.events, Fenrir::Detail::Unlimited, 1000, all);
        for (const Crossing& crossing : all)
        {
            crossed.emplace_back(clip.events[crossing.index].time, crossing.loop);
        }
        return crossed;
    }

    bool Near(double a, double b)
    {
        return std::abs(a - b) < 1e-6;
    }

    PlaybackCursor Started(const Fenrir::ClipData& clip, float speed = 1.0f)
    {
        PlaybackCursor cursor;
        cursor.Start(clip, 1, speed);
        return cursor;
    }
} // namespace

TEST_CASE("forward 0 -> 0.5 crosses [0, 0.5], the event at 0 included, and the next step excludes 0.5",
          "[animation][cursor]")
{
    const auto clip = Clip(Quarters);
    PlaybackCursor cursor = Started(clip);

    REQUIRE(Read(clip, cursor.Step(clip, ClipLoop::Loop, true, 0.5)) == Crossed{{0.0f, 0}, {0.25f, 0}, {0.5f, 0}});
    REQUIRE(Near(cursor.Time(), 0.5));
    REQUIRE(Read(clip, cursor.Step(clip, ClipLoop::Loop, true, 0.25)) == Crossed{{0.75f, 0}});
}

TEST_CASE("forward across a wrap crosses the end of the lap, then the start of the next, in order",
          "[animation][cursor]")
{
    const auto clip = Clip(Quarters);
    PlaybackCursor cursor = Started(clip);
    (void)cursor.Step(clip, ClipLoop::Loop, true, 0.75);

    const CursorStep step = cursor.Step(clip, ClipLoop::Loop, true, 0.5);

    INFO("0.75 -> 1.25 crosses 1 in lap 0, then 0 and 0.25 in lap 1");
    REQUIRE(Read(clip, step) == Crossed{{1.0f, 0}, {0.0f, 1}, {0.25f, 1}});
    REQUIRE(Near(cursor.Time(), 0.25));
    REQUIRE(cursor.CompletedLoops() == 1);
    INFO("a wrap is a jump for the sampler's key cursors");
    REQUIRE(step.jumped);
}

TEST_CASE("reverse crosses in reverse order from the duration, and wraps across 0", "[animation][cursor]")
{
    const auto clip = Clip(Quarters);
    PlaybackCursor cursor = Started(clip, -1.0f);
    REQUIRE(Near(cursor.Time(), 1.0));

    INFO("starting in reverse at the duration crosses [0.25, 1], the event at 1 included, latest first");
    REQUIRE(Read(clip, cursor.Step(clip, ClipLoop::Loop, true, -0.75)) ==
            Crossed{{1.0f, 0}, {0.75f, 0}, {0.5f, 0}, {0.25f, 0}});

    INFO("0.25 -> -0.25 crosses 0, then wraps and crosses 1 and 0.75");
    REQUIRE(Read(clip, cursor.Step(clip, ClipLoop::Loop, true, -0.5)) == Crossed{{0.0f, 0}, {1.0f, 1}, {0.75f, 1}});
    REQUIRE(Near(cursor.Time(), 0.75));
    REQUIRE(cursor.CompletedLoops() == 1);
}

TEST_CASE("a seek crosses nothing, and the next step crosses only what follows it", "[animation][cursor]")
{
    const auto clip = Clip(Quarters);
    PlaybackCursor cursor = Started(clip);
    (void)cursor.Step(clip, ClipLoop::Loop, true, 0.125);

    cursor.Seek(0.25, ClipLoop::Loop);

    INFO("the step from 0.25 crosses (0.25, 0.5]");
    REQUIRE(Read(clip, cursor.Step(clip, ClipLoop::Loop, true, 0.25)) == Crossed{{0.5f, 0}});
    REQUIRE(Near(cursor.Time(), 0.5));
}

TEST_CASE("a seek is clamped by Once and wrapped by Loop", "[animation][cursor]")
{
    const auto clip = Clip({});
    PlaybackCursor once = Started(clip);
    PlaybackCursor loop = Started(clip);

    once.Seek(1.75, ClipLoop::Once);
    loop.Seek(1.75, ClipLoop::Loop);
    REQUIRE(Near(once.Time(), 1.0));
    REQUIRE(Near(loop.Time(), 0.75));
    loop.Seek(-0.25, ClipLoop::Loop);
    REQUIRE(Near(loop.Time(), 0.75));
}

TEST_CASE("a pause or a speed of 0 crosses nothing and holds", "[animation][cursor]")
{
    const auto clip = Clip(Quarters);
    PlaybackCursor cursor = Started(clip);
    (void)cursor.Step(clip, ClipLoop::Loop, true, 0.5);

    REQUIRE(Read(clip, cursor.Step(clip, ClipLoop::Loop, false, 0.0)).empty());
    REQUIRE(Read(clip, cursor.Step(clip, ClipLoop::Loop, true, 0.0)).empty());
    REQUIRE(Near(cursor.Time(), 0.5));
}

TEST_CASE("a start places the cursor at 0, or at the duration in reverse, with no loops", "[animation][cursor]")
{
    const auto clip = Clip({});
    PlaybackCursor forward = Started(clip);
    PlaybackCursor reverse = Started(clip, -1.0f);
    (void)forward.Step(clip, ClipLoop::Loop, true, 0.5);
    (void)reverse.Step(clip, ClipLoop::Loop, true, -0.5);

    forward.Start(clip, 2, 1.0f);
    reverse.Start(clip, 2, -1.0f);
    (void)forward.Step(clip, ClipLoop::Loop, true, 0.125);
    (void)reverse.Step(clip, ClipLoop::Loop, true, -0.125);

    REQUIRE(Near(forward.Time(), 0.125));
    REQUIRE(Near(reverse.Time(), 0.875));
    REQUIRE(reverse.CompletedLoops() == 0);
    REQUIRE(forward.Revision() == 2);
}

TEST_CASE("a refit places the time into the changed duration, and a finished cursor holds its boundary",
          "[animation][cursor]")
{
    const auto clip = Clip({});
    const auto shorter = Clip({}, 0.5f);
    PlaybackCursor loop = Started(clip);
    (void)loop.Step(clip, ClipLoop::Loop, true, 0.75);
    loop.Refit(shorter, 2, ClipLoop::Loop);
    INFO("0.75 wraps into a duration of 0.5");
    REQUIRE(Near(loop.Time(), 0.25));
    REQUIRE(loop.Revision() == 2);

    PlaybackCursor once = Started(clip);
    (void)once.Step(clip, ClipLoop::Once, true, 2.0);
    once.Refit(shorter, 2, ClipLoop::Once);
    INFO("finished at the end, it holds the new end");
    REQUIRE(Near(once.Time(), 0.5));
    REQUIRE(once.Finished());
}

TEST_CASE("a zero-duration clip holds at 0, never wraps, and crosses its events once", "[animation][cursor]")
{
    const auto clip = Clip({0.0f}, 0.0f);
    PlaybackCursor loop = Started(clip);

    Crossed crossed;
    for (int frame = 0; frame < 4; ++frame)
    {
        for (const auto& c : Read(clip, loop.Step(clip, ClipLoop::Loop, true, 0.5)))
        {
            crossed.push_back(c);
        }
    }
    REQUIRE(crossed == Crossed{{0.0f, 0}});
    REQUIRE(loop.Time() == 0.0);
    REQUIRE(loop.CompletedLoops() == 0);

    INFO("a Once one finishes on its first step, and once only");
    PlaybackCursor once = Started(clip);
    REQUIRE(once.Step(clip, ClipLoop::Once, true, 0.5).finishedNow);
    REQUIRE(once.Finished());
    REQUIRE_FALSE(once.Step(clip, ClipLoop::Once, true, 0.5).finishedNow);
}

TEST_CASE("Once clamps at its end, finishes once, and crosses the boundary event once", "[animation][cursor]")
{
    const auto clip = Clip(Quarters);
    PlaybackCursor cursor = Started(clip);

    Crossed crossed;
    int finishes = 0;
    for (int frame = 0; frame < 3; ++frame)
    {
        const CursorStep step = cursor.Step(clip, ClipLoop::Once, true, 0.75);
        finishes += step.finishedNow ? 1 : 0;
        for (const auto& c : Read(clip, step))
        {
            crossed.push_back(c);
        }
        REQUIRE(cursor.Finished() == (frame > 0));
    }
    REQUIRE(Near(cursor.Time(), 1.0));
    REQUIRE(finishes == 1);
    REQUIRE(crossed == Crossed{{0.0f, 0}, {0.25f, 0}, {0.5f, 0}, {0.75f, 0}, {1.0f, 0}});

    INFO("pausing keeps it finished");
    (void)cursor.Step(clip, ClipLoop::Once, false, 0.0);
    REQUIRE(cursor.Finished());
}

TEST_CASE("finished resets on a seek, a start and a reversal away from its boundary", "[animation][cursor]")
{
    const auto clip = Clip({});
    PlaybackCursor cursor = Started(clip);
    REQUIRE(cursor.Step(clip, ClipLoop::Once, true, 2.0).finishedNow);

    cursor.Seek(0.5, ClipLoop::Once);
    REQUIRE_FALSE(cursor.Finished());
    REQUIRE(cursor.Step(clip, ClipLoop::Once, true, 2.0).finishedNow);

    INFO("reversing away from the end resets it and is a jump; reaching 0 finishes again");
    const CursorStep back = cursor.Step(clip, ClipLoop::Once, true, -0.125);
    REQUIRE_FALSE(cursor.Finished());
    REQUIRE(back.jumped);
    REQUIRE(cursor.Step(clip, ClipLoop::Once, true, -2.0).finishedNow);
    REQUIRE(Near(cursor.Time(), 0.0));

    INFO("moving on towards the boundary it holds at keeps it finished, with no second finish");
    REQUIRE_FALSE(cursor.Step(clip, ClipLoop::Once, true, -0.25).finishedNow);
    REQUIRE(cursor.Finished());

    cursor.Start(clip, 2, 1.0f);
    REQUIRE_FALSE(cursor.Finished());
}

TEST_CASE("a step across 100 laps counts them exactly, and its crossings are counted without being listed",
          "[animation][cursor][budget]")
{
    const auto clip = Clip({0.1f, 0.3f, 0.5f, 0.7f, 0.9f});
    PlaybackCursor cursor = Started(clip);

    const CursorStep step = cursor.Step(clip, ClipLoop::Loop, true, 100.0);

    REQUIRE(cursor.CompletedLoops() == 100);
    REQUIRE(step.crossings->CountBefore(clip.events) == 500);
    INFO("the earliest 64 end at lap 12's fourth event");
    std::vector<Crossing> earliest;
    step.crossings->Earliest(clip.events, Fenrir::Detail::Unlimited, 64, earliest);
    REQUIRE(earliest.size() == 64);
    REQUIRE(earliest.front().index == 0);
    REQUIRE(earliest.back().index == 3);
    REQUIRE(earliest.back().loop == 12);
    INFO("a limit along the step counts only what lies before it: 2.4 s covers two laps and two events");
    REQUIRE(step.crossings->CountBefore(clip.events, 2.4) == 12);
}
