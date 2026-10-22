// Input routers: registration, order, consumption and BeginDispatch (ADR-0027; spec: "FenrirInput", seam 3).

#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "InputTestApp.hpp"
#include "TestRouter.hpp"

using Fenrir::Input;
using Fenrir::Key;
using Fenrir::Scancode;
using namespace InputTests;

TEST_CASE("Routers see each input highest priority first, and equal priorities in registration order",
          "[input][routers]")
{
    InputTestApp test;
    Journal journal = NewJournal();
    (void)Input::AddInputRouter(test.app, 0, std::make_shared<TestRouter>("low", journal));
    (void)Input::AddInputRouter(test.app, 10, std::make_shared<TestRouter>("first ten", journal));
    (void)Input::AddInputRouter(test.app, 10, std::make_shared<TestRouter>("second ten", journal));

    test.Press(Key::A, Scancode::A);
    test.app.RunFrame(oneTick);

    INFO("the two priority-10 routers go first, in the order they were added, then the priority-0 one");
    REQUIRE(Routed(journal) == Lines{"first ten: key A down", "second ten: key A down", "low: key A down"});
}

TEST_CASE("A consumed input reaches no lower router, no resource and no game reader", "[input][routers]")
{
    InputTestApp test;
    Journal journal = NewJournal();
    auto ui = std::make_shared<TestRouter>("ui", journal);
    ui->decide = ConsumeAll;
    (void)Input::AddInputRouter(test.app, 10, ui);
    (void)Input::AddInputRouter(test.app, 0, std::make_shared<TestRouter>("lower", journal));
    Fenrir::EventReader<Fenrir::KeyEvent> keys;
    Fenrir::EventReader<Fenrir::MouseMotionEvent> motion;

    test.Press(Key::A, Scancode::A);
    test.Feed().MouseMove(mainWindow, {5.0f, 6.0f}, {1.0f, 1.0f});
    test.app.RunFrame(oneTick);

    INFO("only the consuming router saw the inputs");
    REQUIRE(Routed(journal) == Lines{"ui: key A down", "ui: motion"});
    INFO("the key is not held or just pressed in Keyboard");
    REQUIRE_FALSE(test.Keyboard().IsDown(Key::A));
    REQUIRE_FALSE(test.Keyboard().JustPressed(Scancode::A));
    INFO("the mouse did not move");
    REQUIRE(test.Mouse().Position() == Fenrir::Math::Vec2(0.0f, 0.0f));
    INFO("no game reader got an event");
    REQUIRE(keys.Read(test.app).empty());
    REQUIRE(motion.Read(test.app).empty());
}

TEST_CASE("Input no router consumes reaches the game", "[input][routers]")
{
    InputTestApp test;
    Journal journal = NewJournal();
    (void)Input::AddInputRouter(test.app, 0, std::make_shared<TestRouter>("ui", journal));
    Fenrir::EventReader<Fenrir::KeyEvent> keys;

    test.Press(Key::A, Scancode::A);
    test.app.RunFrame(oneTick);

    INFO("the router saw the key and passed it, so the game holds it and reads its event");
    REQUIRE(Routed(journal) == Lines{"ui: key A down"});
    REQUIRE(test.Keyboard().IsDown(Key::A));
    REQUIRE(ReadDescribed(keys, test.app) == Lines{"key A down"});
}

TEST_CASE("A router added during a dispatch starts the next frame", "[input][routers]")
{
    InputTestApp test;
    Journal journal = NewJournal();
    auto adder = std::make_shared<TestRouter>("adder", journal);
    bool added = false;
    adder->onRoute = [&](Fenrir::App& app, const Fenrir::RoutedInput&) {
        if (!added)
        {
            added = true;
            auto late = std::make_shared<TestRouter>("late", journal);
            late->decide = ConsumeAll;
            (void)Input::AddInputRouter(app, 100, late);
        }
    };
    (void)Input::AddInputRouter(test.app, 0, adder);

    test.Press(Key::A, Scancode::A);
    test.Press(Key::B, Scancode::B);
    test.app.RunFrame(oneTick);

    INFO("the router added while A was routed saw neither A nor B in that frame");
    REQUIRE(Routed(journal) == Lines{"adder: key A down", "adder: key B down"});
    REQUIRE(test.Keyboard().IsDown(Key::B));

    journal->clear();
    test.Press(Key::C, Scancode::C);
    test.app.RunFrame(oneTick);
    INFO("from the next frame it gets its BeginDispatch and, at priority 100, the input first");
    REQUIRE(*journal == Lines{"late: begin", "adder: begin", "late: key C down"});
}

TEST_CASE("A router removed during a dispatch stops at once", "[input][routers]")
{
    InputTestApp test;
    Journal journal = NewJournal();
    auto doomed = std::make_shared<TestRouter>("doomed", journal);
    const Fenrir::InputRouterId doomedId = Input::AddInputRouter(test.app, 0, doomed);
    auto remover = std::make_shared<TestRouter>("remover", journal);
    remover->onRoute = [&](Fenrir::App& app, const Fenrir::RoutedInput&) { Input::RemoveInputRouter(app, doomedId); };
    (void)Input::AddInputRouter(test.app, 10, remover);

    test.Press(Key::A, Scancode::A);
    test.Press(Key::B, Scancode::B);
    test.app.RunFrame(oneTick);

    INFO("the router removed while the higher router saw A sees neither A nor B");
    REQUIRE(Routed(journal) == Lines{"remover: key A down", "remover: key B down"});
    INFO("both keys pass to the game");
    REQUIRE(test.Keyboard().IsDown(Key::A));
    REQUIRE(test.Keyboard().IsDown(Key::B));
}

TEST_CASE("Each router gets one BeginDispatch a frame, before the frame's first input, however often Dispatch runs",
          "[input][routers][dispatch]")
{
    InputTestApp test;
    Journal journal = NewJournal();
    auto ui = std::make_shared<TestRouter>("ui", journal);
    (void)Input::AddInputRouter(test.app, 0, ui);
    // A feeder after FenrirInput's own dispatch, as FenrirWindow's pump is, dispatching twice.
    test.app.AddSequentialSystem(Fenrir::SchedulePriority::PreUpdate, [](Fenrir::App& app) {
        app.GetResource<Fenrir::InputFeed>().KeyDown(mainWindow, Key::A, Scancode::A);
        Input::Dispatch(app);
        app.GetResource<Fenrir::InputFeed>().Text(mainWindow, U"a");
        Input::Dispatch(app);
        Input::Dispatch(app);
    });

    test.app.RunFrame(oneTick);
    INFO("one BeginDispatch, then the frame's input in order");
    REQUIRE(*journal == Lines{"ui: begin", "ui: key A down", "ui: text a"});

    journal->clear();
    test.app.RunFrame(oneTick);
    INFO("the next frame begins once more");
    REQUIRE(ui->begins == 2);
    REQUIRE(journal->front() == "ui: begin");
}

TEST_CASE("A router gets its BeginDispatch in a frame with no input", "[input][routers][dispatch]")
{
    InputTestApp test;
    auto ui = std::make_shared<TestRouter>("ui", NewJournal());
    (void)Input::AddInputRouter(test.app, 0, ui);

    test.app.RunFrame(oneTick);
    test.app.RunFrame(oneTick);

    INFO("two frames with nothing fed still begin two dispatches");
    REQUIRE(ui->begins == 2);
}

TEST_CASE("Adding a router to an app with no input gives no id", "[input][routers]")
{
    Fenrir::App app(std::make_unique<SilentLogger>(), 1);
    const Fenrir::InputRouterId id = Input::AddInputRouter(app, 0, std::make_shared<TestRouter>("ui", NewJournal()));

    INFO("the id names no router");
    REQUIRE(id == Fenrir::InputRouterId{});
}
