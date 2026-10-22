#include <catch2/catch_test_macros.hpp>

#include "DebugDrawTestApp.hpp"

using DebugDrawTest::LineCount;
using DebugDrawTest::TestApp;
using DebugDrawTest::TickRate;
using Fenrir::Colour;
using Fenrir::SchedulePriority;
using Fenrir::Math::Vec3;

namespace
{
    Fenrir::DebugDraw& ActiveDraw(Fenrir::App& app)
    {
        return *app.GetActiveScene().GetResource<Fenrir::DebugDraw>();
    }
} // namespace

TEST_CASE("A line drawn in Tick survives a frame with no tick and is gone after the next tick",
          "[debugdraw][lifetimes]")
{
    TestApp test;
    int ticks = 0;
    test.app.AddSequentialSystem(SchedulePriority::Tick, [&ticks](Fenrir::App& app) {
        // Drawn in the first tick only.
        if (ticks++ == 0)
        {
            ActiveDraw(app).Line(Vec3(0.0f), Vec3(1.0f), Colour::Red);
        }
    });
    test.WatchRender();

    INFO("the frame whose tick drew the line shows it");
    REQUIRE(LineCount(test.Frame(TickRate)) == 1);
    INFO("a frame with no tick still shows it");
    REQUIRE(LineCount(test.FrameWithNoTick()) == 1);
    INFO("after the next tick, which drew nothing, it is gone");
    REQUIRE(LineCount(test.Frame(TickRate)) == 0);
    REQUIRE(ticks == 2);
}

TEST_CASE("A line drawn every tick does not double in a frame with two ticks", "[debugdraw][lifetimes]")
{
    TestApp test;
    int ticks = 0;
    test.app.AddSequentialSystem(SchedulePriority::Tick, [&ticks](Fenrir::App& app) {
        ++ticks;
        ActiveDraw(app).Line(Vec3(0.0f), Vec3(1.0f), Colour::Red);
    });
    test.WatchRender();

    const std::size_t lines = LineCount(test.Frame(TickRate * 2.0));

    INFO("the frame ran two ticks");
    REQUIRE(ticks == 2);
    INFO("only the second tick's line is shown");
    REQUIRE(lines == 1);
}

TEST_CASE("A line drawn from a parallel PreTick system lasts until the next tick too", "[debugdraw][lifetimes]")
{
    TestApp test;
    int ticks = 0;
    test.app.AddSystem(SchedulePriority::PreTick, [&ticks](Fenrir::App& app) {
        if (ticks++ == 0)
        {
            ActiveDraw(app).Line(Vec3(0.0f), Vec3(1.0f), Colour::Red);
        }
    });
    test.WatchRender();

    INFO("the frame whose tick drew the line shows it");
    REQUIRE(LineCount(test.Frame(TickRate)) == 1);
    INFO("a frame with no tick still shows it");
    REQUIRE(LineCount(test.FrameWithNoTick()) == 1);
    INFO("after the next tick it is gone");
    REQUIRE(LineCount(test.Frame(TickRate)) == 0);
}

TEST_CASE("A line drawn in Update lasts to the end of that frame's Render", "[debugdraw][lifetimes]")
{
    TestApp test;
    int frames = 0;
    test.app.AddSequentialSystem(SchedulePriority::Update, [&frames](Fenrir::App& app) {
        if (frames++ == 0)
        {
            ActiveDraw(app).Line(Vec3(0.0f), Vec3(1.0f), Colour::Red);
        }
    });
    std::size_t seenInPostUpdate = 0;
    test.app.AddSequentialSystem(SchedulePriority::PostUpdate, [&seenInPostUpdate](Fenrir::App& app) {
        seenInPostUpdate = LineCount(ActiveDraw(app).Tessellate());
    });
    test.WatchRender();

    INFO("the frame that drew it shows it in Render");
    REQUIRE(LineCount(test.FrameWithNoTick()) == 1);
    INFO("the next frame, with no tick, does not");
    REQUIRE(LineCount(test.FrameWithNoTick()) == 0);
    INFO("nor even before its Render");
    REQUIRE(seenInPostUpdate == 0);
}

TEST_CASE("A line with a duration in seconds expires on frame time", "[debugdraw][lifetimes]")
{
    TestApp test;
    int frames = 0;
    test.app.AddSequentialSystem(SchedulePriority::Update, [&frames](Fenrir::App& app) {
        if (frames++ == 0)
        {
            ActiveDraw(app).Line(Vec3(0.0f), Vec3(1.0f), Colour::Red, {.duration = 1.0f});
        }
    });
    test.WatchRender();

    // Frames a quarter of a second long: the line, drawn in the first, is kept a second of frame time after it.
    for (int frame = 1; frame <= 4; ++frame)
    {
        INFO("frame " << frame << " is within the line's second");
        REQUIRE(LineCount(test.Frame(0.25)) == 1);
    }
    INFO("the fifth frame is a second after the first, and the line is gone");
    REQUIRE(LineCount(test.Frame(0.25)) == 0);
}

TEST_CASE("A line drawn Forever lasts until Clear", "[debugdraw][lifetimes]")
{
    TestApp test;
    int frames = 0;
    test.app.AddSequentialSystem(SchedulePriority::Update, [&frames](Fenrir::App& app) {
        ++frames;
        if (frames == 1)
        {
            ActiveDraw(app).Line(Vec3(0.0f), Vec3(1.0f), Colour::Red, {.duration = Fenrir::DebugDraw::Forever});
        }
        if (frames == 4)
        {
            ActiveDraw(app).Clear();
        }
    });
    test.WatchRender();

    for (int frame = 1; frame <= 3; ++frame)
    {
        INFO("frame " << frame << " still shows the line");
        REQUIRE(LineCount(test.Frame(10.0)) == 1);
    }
    INFO("the frame that cleared it does not");
    REQUIRE(LineCount(test.Frame(10.0)) == 0);
}
