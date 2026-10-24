#include <catch2/catch_test_macros.hpp>

#include "DebugDrawTestApp.hpp"
#include "FenrirECS/DefaultComponents.hpp"

using DebugDrawTest::LineCount;
using DebugDrawTest::Near;
using DebugDrawTest::TestApp;
using Fenrir::Colour;
using Fenrir::DebugDraw;
using Fenrir::SchedulePriority;
using Fenrir::Math::Vec3;

TEST_CASE("An item relative to an entity follows its world transform and is gone when it is destroyed",
          "[debugdraw][relativeTo]")
{
    TestApp test;
    Fenrir::Entity mover = test.app.GetActiveScene().GetEntityList().CreateEntity();
    mover.AddComponent<Fenrir::Transform>().pos = Vec3(10.0f, 0.0f, 0.0f);
    int frames = 0;
    test.app.AddSequentialSystem(SchedulePriority::Update, [&frames, mover](Fenrir::App& app) {
        if (frames++ == 0)
        {
            DebugDraw& draw = *app.GetActiveScene().GetResource<DebugDraw>();
            draw.Line(Vec3(0.0f), Vec3(1.0f, 0.0f, 0.0f), Colour::Red,
                      {.duration = DebugDraw::Forever, .relativeTo = mover});
            draw.WorldText(Vec3(0.0f, 1.0f, 0.0f), "mover", Colour::White,
                           {.duration = DebugDraw::Forever, .relativeTo = mover});
        }
    });
    test.WatchRender();

    const Fenrir::DebugBatches first = test.FrameWithNoTick();
    INFO("the line is placed by the entity's transform: from (10, 0, 0) to (11, 0, 0)");
    REQUIRE(LineCount(first) == 1);
    REQUIRE(Near(first.lines[0].vertices[0].position, Vec3(10.0f, 0.0f, 0.0f)));
    REQUIRE(Near(first.lines[0].vertices[1].position, Vec3(11.0f, 0.0f, 0.0f)));
    INFO("the world text too: at (10, 1, 0)");
    REQUIRE(first.texts.size() == 1);
    REQUIRE(Near(first.texts[0].position, Vec3(10.0f, 1.0f, 0.0f)));

    mover.GetComponent<Fenrir::Transform>().pos = Vec3(0.0f, 5.0f, 0.0f);
    const Fenrir::DebugBatches moved = test.FrameWithNoTick();
    INFO("when the entity moves the line follows it: from (0, 5, 0) to (1, 5, 0)");
    REQUIRE(LineCount(moved) == 1);
    REQUIRE(Near(moved.lines[0].vertices[0].position, Vec3(0.0f, 5.0f, 0.0f)));
    REQUIRE(Near(moved.lines[0].vertices[1].position, Vec3(1.0f, 5.0f, 0.0f)));

    test.app.GetActiveScene().GetEntityList().DestroyEntity(mover);
    const Fenrir::DebugBatches gone = test.FrameWithNoTick();
    INFO("once the entity is destroyed its items are gone, though drawn Forever");
    REQUIRE(gone.Empty());
}

TEST_CASE("An item drawn relative to an entity already destroyed is not recorded", "[debugdraw][relativeTo]")
{
    TestApp test;
    Fenrir::Entity gone = test.app.GetActiveScene().GetEntityList().CreateEntity();
    test.app.GetActiveScene().GetEntityList().DestroyEntity(gone);
    test.app.AddSequentialSystem(SchedulePriority::Update, [gone](Fenrir::App& app) {
        app.GetActiveScene().GetResource<DebugDraw>()->Line(Vec3(0.0f), Vec3(1.0f), Colour::Red, {.relativeTo = gone});
    });
    test.WatchRender();

    INFO("nothing is drawn");
    REQUIRE(test.FrameWithNoTick().Empty());
}
