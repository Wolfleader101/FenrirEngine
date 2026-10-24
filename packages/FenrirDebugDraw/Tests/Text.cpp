#include <catch2/catch_test_macros.hpp>

#include "DebugDrawTestApp.hpp"

using DebugDrawTest::TestApp;
using Fenrir::Colour;
using Fenrir::DebugDraw;
using Fenrir::DebugTextSpace;
using Fenrir::SchedulePriority;
using Fenrir::Math::Vec3;

TEST_CASE("On-screen messages stack newest first, and a key replaces the message under it", "[debugdraw][text]")
{
    TestApp test;
    int frames = 0;
    test.app.AddSequentialSystem(SchedulePriority::Update, [&frames](Fenrir::App& app) {
        DebugDraw& draw = *app.GetActiveScene().GetResource<DebugDraw>();
        ++frames;
        if (frames == 1)
        {
            draw.Message("first", Colour::White, {.duration = DebugDraw::Forever});
            draw.Message(7, "keyed", Colour::White, {.duration = DebugDraw::Forever});
            draw.Message("third", Colour::White, {.duration = DebugDraw::Forever});
        }
        if (frames == 2)
        {
            draw.Message(7, "keyed again", Colour::Yellow, {.duration = DebugDraw::Forever});
        }
    });
    test.WatchRender();

    const Fenrir::DebugBatches first = test.FrameWithNoTick();
    INFO("three messages, newest first");
    REQUIRE(first.texts.size() == 3);
    REQUIRE(first.texts[0].text == "third");
    REQUIRE(first.texts[1].text == "keyed");
    REQUIRE(first.texts[2].text == "first");
    REQUIRE(first.texts[0].space == DebugTextSpace::Message);

    const Fenrir::DebugBatches second = test.FrameWithNoTick();
    INFO("the keyed message is replaced where it stood, not added");
    REQUIRE(second.texts.size() == 3);
    REQUIRE(second.texts[0].text == "third");
    REQUIRE(second.texts[1].text == "keyed again");
    REQUIRE(second.texts[1].colour.b == 0.0f);
    REQUIRE(second.texts[2].text == "first");
}

TEST_CASE("Screen text, messages and world text are text runs in that order", "[debugdraw][text]")
{
    TestApp test;
    test.app.AddSequentialSystem(SchedulePriority::Update, [](Fenrir::App& app) {
        DebugDraw& draw = *app.GetActiveScene().GetResource<DebugDraw>();
        draw.WorldText(Vec3(1.0f, 2.0f, 3.0f), "world", Colour::Green, {.size = 24.0f});
        draw.Message("message", Colour::White);
        draw.ScreenText(Fenrir::Math::Vec2(10.0f, 20.0f), "screen", Colour::Red);
    });
    test.WatchRender();

    const Fenrir::DebugBatches& seen = test.FrameWithNoTick();

    INFO("three runs, and no shapes");
    REQUIRE(seen.texts.size() == 3);
    REQUIRE(seen.lines.empty());
    INFO("screen text first, at its pixel position, at the default size");
    REQUIRE(seen.texts[0].space == DebugTextSpace::Screen);
    REQUIRE(seen.texts[0].text == "screen");
    REQUIRE(seen.texts[0].position == Vec3(10.0f, 20.0f, 0.0f));
    REQUIRE(seen.texts[0].size == 16.0f);
    INFO("then the message");
    REQUIRE(seen.texts[1].space == DebugTextSpace::Message);
    INFO("then world text at its point and size");
    REQUIRE(seen.texts[2].space == DebugTextSpace::World);
    REQUIRE(seen.texts[2].position == Vec3(1.0f, 2.0f, 3.0f));
    REQUIRE(seen.texts[2].size == 24.0f);
}

TEST_CASE("A scene made after Install gets a DebugDraw at the start of the next frame", "[debugdraw][scenes]")
{
    TestApp test;
    Fenrir::Scene& later = test.app.CreateScene("Later");

    INFO("the new scene has none until a frame starts");
    REQUIRE(later.GetResource<DebugDraw>() == nullptr);
    test.FrameWithNoTick();
    INFO("after a frame it has one");
    REQUIRE(later.GetResource<DebugDraw>() != nullptr);
}

TEST_CASE("Destroying the owner removes every scene's DebugDraw", "[debugdraw][scenes]")
{
    TestApp test;
    test.FrameWithNoTick();
    REQUIRE(test.app.GetActiveScene().GetResource<DebugDraw>() != nullptr);

    {
        const Fenrir::DebugDrawing ended = std::move(test.drawing);
        INFO("a moved-from owner is not valid; the one it moved to is");
        REQUIRE_FALSE(test.drawing.IsValid());
        REQUIRE(ended.IsValid());
    }
    INFO("once the owner is gone the scene has no DebugDraw, and frames still run");
    REQUIRE(test.app.GetActiveScene().GetResource<DebugDraw>() == nullptr);
    test.FrameWithNoTick();
    REQUIRE(test.app.GetActiveScene().GetResource<DebugDraw>() == nullptr);
}
