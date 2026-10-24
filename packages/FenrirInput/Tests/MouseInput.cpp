// The mouse's buttons, position, motion and scroll, in their documented units (ADR-0017).

#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "InputTestApp.hpp"

using Fenrir::MouseButton;
using Fenrir::SchedulePriority;
using Fenrir::Math::Vec2;
using namespace InputTests;

TEST_CASE("Mouse position is where the pointer last was and motion sums the frame's moves", "[input][mouse]")
{
    InputTestApp test;
    test.Feed().MouseMove(mainWindow, Vec2(10.0f, 20.0f), Vec2(3.0f, 4.0f));
    test.Feed().MouseMove(mainWindow, Vec2(15.5f, 22.0f), Vec2(5.5f, 2.0f));
    test.app.RunFrame(oneTick);

    const Fenrir::Mouse& mouse = test.Mouse();
    INFO("the position is the last one fed, in logical coordinates from the window's top-left corner");
    REQUIRE(mouse.Position() == Vec2(15.5f, 22.0f));
    INFO("the motion is the sum of the frame's moves, in the same units");
    REQUIRE(mouse.Motion() == Vec2(8.5f, 6.0f));

    test.app.RunFrame(oneTick);
    INFO("a frame with no move has no motion");
    REQUIRE(mouse.Motion() == Vec2(0.0f, 0.0f));
    INFO("and leaves the position where it was");
    REQUIRE(mouse.Position() == Vec2(15.5f, 22.0f));
}

TEST_CASE("Scroll is the frame's wheel turns in notches", "[input][mouse]")
{
    InputTestApp test;
    test.Feed().Scroll(mainWindow, Vec2(0.0f, 1.0f));
    test.Feed().Scroll(mainWindow, Vec2(0.0f, 2.0f));
    test.app.RunFrame(oneTick);
    INFO("two turns of one and two notches scroll three");
    REQUIRE(test.Mouse().Scroll() == Vec2(0.0f, 3.0f));

    test.app.RunFrame(oneTick);
    INFO("a frame with no turn scrolls nothing");
    REQUIRE(test.Mouse().Scroll() == Vec2(0.0f, 0.0f));
}

TEST_CASE("Motion read in Tick sums the moves since the last tick", "[input][mouse]")
{
    InputTestApp test;
    auto motions = std::make_shared<std::vector<Vec2>>();
    test.app.AddSystem(SchedulePriority::Tick,
                       [motions](Fenrir::App& app) { motions->push_back(app.GetResource<Fenrir::Mouse>().Motion()); });

    test.Feed().MouseMove(mainWindow, Vec2(1.0f, 0.0f), Vec2(1.0f, 0.0f));
    test.app.RunFrame(noTick);
    test.Feed().MouseMove(mainWindow, Vec2(3.0f, 0.0f), Vec2(2.0f, 0.0f));
    test.app.RunFrame(twoTicks);

    REQUIRE(motions->size() == 2);
    INFO("the first tick reads the moves of both frames since the last tick");
    REQUIRE((*motions)[0] == Vec2(3.0f, 0.0f));
    INFO("the second tick of the frame reads none");
    REQUIRE((*motions)[1] == Vec2(0.0f, 0.0f));
}

TEST_CASE("Mouse buttons are held, just pressed and just released", "[input][mouse]")
{
    InputTestApp test;
    test.Feed().MouseButtonDown(mainWindow, MouseButton::Right);
    test.app.RunFrame(oneTick);

    const Fenrir::Mouse& mouse = test.Mouse();
    INFO("the right button is held and just pressed");
    REQUIRE(mouse.IsDown(MouseButton::Right));
    REQUIRE(mouse.JustPressed(MouseButton::Right));
    INFO("the left button is not");
    REQUIRE_FALSE(mouse.IsDown(MouseButton::Left));

    test.Feed().MouseButtonUp(mainWindow, MouseButton::Right);
    test.app.RunFrame(oneTick);
    INFO("released, it is just released and no longer held");
    REQUIRE(mouse.JustReleased(MouseButton::Right));
    REQUIRE_FALSE(mouse.IsDown(MouseButton::Right));
}

TEST_CASE("Only the focused window moves the pointer", "[input][mouse]")
{
    InputTestApp test;
    test.Feed().FocusGained(mainWindow);
    test.Feed().MouseMove(mainWindow, Vec2(5.0f, 5.0f), Vec2(1.0f, 1.0f));
    test.Feed().MouseMove(otherWindow, Vec2(90.0f, 90.0f), Vec2(7.0f, 7.0f));
    test.app.RunFrame(oneTick);

    INFO("the position is in the focused window's coordinates");
    REQUIRE(test.Mouse().Position() == Vec2(5.0f, 5.0f));
    INFO("and the motion is the focused window's only");
    REQUIRE(test.Mouse().Motion() == Vec2(1.0f, 1.0f));
}
