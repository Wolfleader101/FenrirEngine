// Control owners: a press belongs to whoever took it, and its repeats and release go to that owner alone; motion,
// scroll and text are dispatched per event; pointer capture puts its holder first (ADR-0027; spec: seam 3).

#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "InputTestApp.hpp"
#include "TestRouter.hpp"

using Fenrir::Input;
using Fenrir::Key;
using Fenrir::MouseButton;
using Fenrir::Scancode;
using namespace InputTests;

TEST_CASE("A press a router took sends its repeats and release to that router alone", "[input][owners]")
{
    InputTestApp test;
    Journal journal = NewJournal();
    auto ui = std::make_shared<TestRouter>("ui", journal);
    // Takes presses only: the repeat and release it is given it passes, yet they must not reach anyone else.
    ui->decide = ConsumePresses;
    (void)Input::AddInputRouter(test.app, 10, ui);
    (void)Input::AddInputRouter(test.app, 0, std::make_shared<TestRouter>("lower", journal));
    Fenrir::EventReader<Fenrir::KeyEvent> keys;

    test.Press(Key::A, Scancode::A);
    test.Feed().KeyDown(mainWindow, Key::A, Scancode::A, Fenrir::KeyModifiers::None, true);
    test.Release(Key::A, Scancode::A);
    test.app.RunFrame(oneTick);

    INFO("the press, its repeat and its release all went to the router that took the press, and to no other router");
    REQUIRE(Routed(journal) == Lines{"ui: key A down", "ui: key A repeat", "ui: key A up"});
    INFO("the game never saw the key");
    REQUIRE(keys.Read(test.app).empty());
    REQUIRE_FALSE(test.Keyboard().JustReleased(Key::A));
}

TEST_CASE("A press the game took still releases to the game after a router appears", "[input][owners]")
{
    InputTestApp test;
    Fenrir::EventReader<Fenrir::KeyEvent> keys;
    test.Press(Key::A, Scancode::A);
    test.app.RunFrame(oneTick);
    INFO("with no router, the game took the press");
    REQUIRE(ReadDescribed(keys, test.app) == Lines{"key A down"});

    Journal journal = NewJournal();
    auto ui = std::make_shared<TestRouter>("ui", journal);
    ui->decide = ConsumeAll;
    (void)Input::AddInputRouter(test.app, 10, ui);
    test.app.RunFrame(oneTick);
    test.Feed().KeyDown(mainWindow, Key::A, Scancode::A, Fenrir::KeyModifiers::None, true);
    test.Release(Key::A, Scancode::A);
    test.app.RunFrame(oneTick);

    INFO("the router that consumes everything saw neither the repeat nor the release");
    REQUIRE(Routed(journal).empty());
    INFO("the game got the repeat and the release");
    REQUIRE(ReadDescribed(keys, test.app) == Lines{"key A repeat", "key A up"});
    REQUIRE(test.Keyboard().JustReleased(Key::A));
}

TEST_CASE("A mouse button press a router took releases to that router alone", "[input][owners]")
{
    InputTestApp test;
    Journal journal = NewJournal();
    auto ui = std::make_shared<TestRouter>("ui", journal);
    ui->decide = ConsumePresses;
    (void)Input::AddInputRouter(test.app, 0, ui);
    Fenrir::EventReader<Fenrir::MouseButtonEvent> buttons;

    test.Feed().MouseButtonDown(mainWindow, MouseButton::Left);
    test.Feed().MouseButtonUp(mainWindow, MouseButton::Left);
    test.app.RunFrame(oneTick);

    INFO("the router got the press and the release; the game got neither");
    REQUIRE(Routed(journal) == Lines{"ui: button Left down", "ui: button Left up"});
    REQUIRE(buttons.Read(test.app).empty());
    REQUIRE_FALSE(test.Mouse().JustPressed(MouseButton::Left));
}

TEST_CASE("Motion, scroll and text are dispatched event by event, not owned", "[input][owners]")
{
    InputTestApp test;
    Journal journal = NewJournal();
    auto ui = std::make_shared<TestRouter>("ui", journal);
    int seen = 0;
    // Consumes the first of each pair only.
    ui->decide = [&seen](const Fenrir::RoutedInput&) {
        return (seen++ % 2 == 0) ? Fenrir::RouteResult::Consumed : Fenrir::RouteResult::Passed;
    };
    (void)Input::AddInputRouter(test.app, 0, ui);
    Fenrir::EventReader<Fenrir::MouseMotionEvent> motion;
    Fenrir::EventReader<Fenrir::MouseScrollEvent> scroll;
    Fenrir::EventReader<Fenrir::TextEvent> text;

    test.Feed().MouseMove(mainWindow, {1.0f, 1.0f}, {1.0f, 1.0f});
    test.Feed().MouseMove(mainWindow, {3.0f, 4.0f}, {2.0f, 3.0f});
    test.Feed().Scroll(mainWindow, {0.0f, 1.0f});
    test.Feed().Scroll(mainWindow, {0.0f, 2.0f});
    test.Feed().Text(mainWindow, U"ab");
    test.app.RunFrame(oneTick);

    INFO("the router saw all six inputs");
    REQUIRE(Routed(journal) ==
            Lines{"ui: motion", "ui: motion", "ui: scroll", "ui: scroll", "ui: text a", "ui: text b"});
    INFO("the second of each pair reached the game");
    REQUIRE(motion.Read(test.app).size() == 1);
    REQUIRE(test.Mouse().Motion() == Fenrir::Math::Vec2(2.0f, 3.0f));
    REQUIRE(scroll.Read(test.app).size() == 1);
    REQUIRE(test.Mouse().Scroll() == Fenrir::Math::Vec2(0.0f, 2.0f));
    REQUIRE(ReadDescribed(text, test.app) == Lines{"text b"});
}

TEST_CASE("While a router holds pointer capture, the window's pointer input reaches it first", "[input][owners]")
{
    InputTestApp test;
    Journal journal = NewJournal();
    (void)Input::AddInputRouter(test.app, 10, std::make_shared<TestRouter>("higher", journal));
    auto dragger = std::make_shared<TestRouter>("dragger", journal);
    const Fenrir::InputRouterId draggerId = Input::AddInputRouter(test.app, 0, dragger);
    test.app.RunFrame(oneTick);
    Fenrir::WindowInputClaims capture;
    capture.pointerCapture = true;
    Input::SetInputClaims(test.app, draggerId, mainWindow, capture);
    Fenrir::EventReader<Fenrir::MouseMotionEvent> motion;

    test.Feed().MouseMove(mainWindow, {1.0f, 1.0f}, {1.0f, 1.0f});
    test.Feed().Scroll(mainWindow, {0.0f, 1.0f});
    test.Feed().MouseMove(otherWindow, {2.0f, 2.0f}, {1.0f, 1.0f});
    test.Press(Key::A, Scancode::A);
    test.app.RunFrame(oneTick);

    INFO("the main window's pointer input went to the holder alone; the other window's and the key went in priority "
         "order");
    REQUIRE(Routed(journal) == Lines{"dragger: motion", "dragger: scroll", "higher: motion", "dragger: motion",
                                     "higher: key A down", "dragger: key A down"});
    INFO("what the holder passed went on to the game");
    REQUIRE(motion.Read(test.app).size() == 2);
}
