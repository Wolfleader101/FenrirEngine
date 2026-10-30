// Tick-posed skeletons (ADR-0040): playback advancing by Time::tickRate per tick that runs, events fired and delivered
// per tick, crossfades progressing per tick, and switching back to frame timing. Frame deltas leave a quarter or a half
// tick over, so no tick count hangs on the accumulator's rounding.
#include <cstdint>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "AnimationTest.hpp"
#include "FenrirScheduler/Scheduler.hpp"

using AnimationTest::AnimationApp;
using AnimationTest::Near;
using Fenrir::Entity;
using Fenrir::PoseKind;
using Fenrir::PoseTiming;
using Fenrir::Skeletons;

namespace
{
    constexpr double Tick = 1.0 / 60.0;

    Entity TickPosed(AnimationApp& test, Fenrir::AssetHandle<Fenrir::Clip> clip)
    {
        Entity skeleton = AnimationTest::Animated(test.level, AnimationTest::AddChain(test), clip);
        skeleton.GetComponent<Fenrir::SkeletonPose>().timing = PoseTiming::Tick;
        return skeleton;
    }

    float TipX(AnimationApp& test, Entity skeleton, PoseKind kind)
    {
        return Skeletons::GetBaseLocal(test.level, skeleton, 2, kind)->pos.x;
    }

    // Each tick's events, as a Tick reader sees them: the tick's number and the authored times read.
    struct TickReader
    {
        Fenrir::EventReader<Fenrir::AnimationEvent> reader;
        std::vector<std::pair<std::uint64_t, std::vector<float>>> ticks;

        explicit TickReader(AnimationApp& test)
        {
            test.app.AddSequentialSystem(Fenrir::SchedulePriority::Tick, [this, &test](Fenrir::App& app) {
                ticks.emplace_back(app.GetTime().tickCount, AnimationTest::ReadTimes(test, reader));
            });
        }
    };
} // namespace

TEST_CASE("a tick-posed skeleton's clip advances by tickRate per tick that runs", "[animation][tick]")
{
    AnimationApp test;
    const Fenrir::AssetHandle<Fenrir::Clip> slide = AnimationTest::AddSlide(test);
    const Entity ticked = TickPosed(test, slide);
    const Entity framed = AnimationTest::Animated(test.level, AnimationTest::AddChain(test), slide);

    test.RunFrame(1.5 * Tick);
    INFO("one tick: the tick-posed clock is at one tickRate, the frame-posed one at the frame's delta");
    REQUIRE(Near(Fenrir::Animation::Playback(test.level, ticked).time, Tick));
    REQUIRE(Near(Fenrir::Animation::Playback(test.level, framed).time, 1.5 * Tick));
    INFO("the simulation pose samples the clip at tick time; the render pose is halfway from the seeded rest pose");
    REQUIRE(Near(TipX(test, ticked, PoseKind::Simulation), static_cast<float>(Tick)));
    REQUIRE(Near(TipX(test, ticked, PoseKind::Render), static_cast<float>(0.5 * Tick)));

    test.RunFrame(0.25 * Tick);
    INFO("a frame with no tick advances nothing");
    REQUIRE(Near(Fenrir::Animation::Playback(test.level, ticked).time, Tick));

    test.RunFrame(10.0 * Tick);
    INFO("ten ticks owed and five run: the clock moves with the five, as physics does");
    REQUIRE(Near(Fenrir::Animation::Playback(test.level, ticked).time, 6.0 * Tick));
}

TEST_CASE("a tick-posed skeleton's events fire in the tick whose step crosses them, read in that Tick",
          "[animation][tick]")
{
    AnimationApp test;
    TickReader read(test);
    // Tick 3 steps over (2/60, 3/60], tick 4 over (3/60, 4/60].
    TickPosed(test, AnimationTest::AddSlide(test, {0.04f, 0.06f}));

    test.RunFrame(4.25 * Tick);

    INFO("four ticks in one frame, each reading only its own crossings");
    using Ticks = std::vector<std::pair<std::uint64_t, std::vector<float>>>;
    REQUIRE(read.ticks == Ticks{{1, {}}, {2, {}}, {3, {0.04f}}, {4, {0.06f}}});
}

TEST_CASE("a tick-posed skeleton's event budget is per tick", "[animation][tick]")
{
    Fenrir::AnimationConfig config;
    config.maxEventsPerInstancePerFrame = 1;
    AnimationApp test(config);
    TickReader read(test);
    Fenrir::EventReader<Fenrir::AnimationEventsDropped> dropped;
    // Two events in each of the first two ticks.
    TickPosed(test, AnimationTest::AddSlide(test, {0.005f, 0.01f, 0.02f, 0.025f}));

    test.RunFrame(2.25 * Tick);

    INFO("each tick sends its earliest event and counts the other as dropped");
    using Ticks = std::vector<std::pair<std::uint64_t, std::vector<float>>>;
    REQUIRE(read.ticks == Ticks{{1, {0.005f}}, {2, {0.02f}}});
    std::uint64_t total = 0;
    for (const Fenrir::AnimationEventsDropped& event : dropped.Read(test.app))
    {
        total += event.dropped;
    }
    REQUIRE(total == 2);
}

TEST_CASE("a tick-posed crossfade progresses per tick", "[animation][tick]")
{
    AnimationApp test;
    Entity skeleton = TickPosed(test, AnimationTest::AddSlide(test));
    test.RunFrame(1.5 * Tick);

    Fenrir::Animator& animator = skeleton.GetComponent<Fenrir::Animator>();
    animator.clip = AnimationTest::AddSlide(test);
    animator.fadeSeconds = 0.1f;
    test.RunFrame(2.0 * Tick);

    INFO("written between frames, seen by the next frame's first tick: two ticks of fade");
    const double fade = static_cast<double>(animator.fadeSeconds);
    Fenrir::PlaybackInfo info = Fenrir::Animation::Playback(test.level, skeleton);
    REQUIRE(info.fading);
    REQUIRE(Near(info.fadeProgress, static_cast<float>(2.0 * Tick / fade)));

    test.RunFrame(0.25 * Tick);
    INFO("no tick, no progress");
    REQUIRE(
        Near(Fenrir::Animation::Playback(test.level, skeleton).fadeProgress, static_cast<float>(2.0 * Tick / fade)));
}

TEST_CASE("switching a tick-posed skeleton back to frame timing resumes its clock where the ticks left it",
          "[animation][tick]")
{
    AnimationApp test;
    Entity skeleton = TickPosed(test, AnimationTest::AddSlide(test));
    test.RunFrame(2.25 * Tick);
    REQUIRE(Near(Fenrir::Animation::Playback(test.level, skeleton).time, 2.0 * Tick));

    skeleton.GetComponent<Fenrir::SkeletonPose>().timing = PoseTiming::Frame;
    test.RunFrame(AnimationTest::Eighth);

    INFO("the frame pass steps on from the tick clock by the frame's delta");
    REQUIRE(Near(Fenrir::Animation::Playback(test.level, skeleton).time, 2.0 * Tick + AnimationTest::Eighth));
    REQUIRE(Near(TipX(test, skeleton, PoseKind::Render), static_cast<float>(2.0 * Tick + AnimationTest::Eighth)));
}
