#include <catch2/catch_test_macros.hpp>

#include "DebugDrawTestApp.hpp"

using DebugDrawTest::LineCount;
using DebugDrawTest::TestApp;
using Fenrir::Colour;
using Fenrir::DebugDraw;
using Fenrir::SchedulePriority;
using Fenrir::Math::Vec3;

TEST_CASE("A hidden group still records, and its items reappear when it is shown", "[debugdraw][groups]")
{
    TestApp test;
    int frames = 0;
    test.app.AddSequentialSystem(SchedulePriority::Update, [&frames](Fenrir::App& app) {
        DebugDraw& draw = *app.GetActiveScene().GetResource<DebugDraw>();
        ++frames;
        if (frames == 1)
        {
            draw.HideGroup("Path");
            draw.Line(Vec3(0.0f), Vec3(1.0f), Colour::Red, {.group = "Path", .duration = DebugDraw::Forever});
        }
        if (frames == 3)
        {
            draw.ShowGroup("Path");
        }
    });
    test.WatchRender();

    INFO("while the group is hidden its line is not shown");
    REQUIRE(LineCount(test.FrameWithNoTick()) == 0);
    REQUIRE_FALSE(test.Draw().IsGroupShown("Path"));
    REQUIRE(LineCount(test.FrameWithNoTick()) == 0);
    INFO("once it is shown the line drawn while it was hidden appears");
    REQUIRE(LineCount(test.FrameWithNoTick()) == 1);
    REQUIRE(test.Draw().IsGroupShown("Path"));
}

TEST_CASE("Clearing a group drops only its items", "[debugdraw][groups]")
{
    TestApp test;
    int frames = 0;
    test.app.AddSequentialSystem(SchedulePriority::Update, [&frames](Fenrir::App& app) {
        DebugDraw& draw = *app.GetActiveScene().GetResource<DebugDraw>();
        ++frames;
        if (frames == 1)
        {
            draw.Line(Vec3(0.0f), Vec3(1.0f), Colour::Red, {.group = "Path", .duration = DebugDraw::Forever});
            draw.Line(Vec3(0.0f), Vec3(2.0f), Colour::Red, {.duration = DebugDraw::Forever});
        }
        if (frames == 2)
        {
            draw.Clear("Path");
        }
    });
    test.WatchRender();

    INFO("both lines are shown before the clear");
    REQUIRE(LineCount(test.FrameWithNoTick()) == 2);
    const Fenrir::DebugBatches& after = test.FrameWithNoTick();
    INFO("after clearing Path only the Default group's line is left");
    REQUIRE(LineCount(after) == 1);
    REQUIRE(after.lines[0].vertices[1].position == Vec3(2.0f));
}
