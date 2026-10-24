// Input::Dispatch: a feeder that runs after FenrirInput's own dispatch, such as a window's pump installed later, still
// gets its input to the game in the same frame (spec: "FenrirWindow", settled 2026-10-22).

#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "InputTestApp.hpp"

using Fenrir::Key;
using Fenrir::Scancode;
using namespace InputTests;

TEST_CASE("Input fed after the dispatch and dispatched by its feeder reaches the game in the same frame",
          "[input][dispatch]")
{
    InputTestApp test;
    bool feedThisFrame = true;
    // Added after Input::Install, so it runs after FenrirInput's own dispatch, as a pump installed later would.
    test.app.AddSequentialSystem(Fenrir::SchedulePriority::PreUpdate, [&feedThisFrame](Fenrir::App& app) {
        if (feedThisFrame)
        {
            app.GetResource<Fenrir::InputFeed>().KeyDown(mainWindow, Key::Space, Scancode::Space);
            Fenrir::Input::Dispatch(app);
        }
    });
    bool heldInBeforeTicks = false;
    test.app.AddSequentialSystem(Fenrir::SchedulePriority::BeforeTicks, [&heldInBeforeTicks](Fenrir::App& app) {
        heldInBeforeTicks = app.GetResource<Fenrir::Keyboard>().IsDown(Key::Space);
    });

    test.app.RunFrame(oneTick);

    INFO("the key fed and dispatched in PreUpdate is held by BeforeTicks of the same frame");
    REQUIRE(heldInBeforeTicks);
    INFO("it is just pressed in that frame");
    REQUIRE(test.Keyboard().JustPressed(Key::Space));
    Fenrir::EventReader<Fenrir::KeyEvent> reader;
    INFO("its event is sent once");
    REQUIRE(reader.Read(test.app).size() == 1);
}

TEST_CASE("Dispatching with nothing fed since the last dispatch does nothing", "[input][dispatch]")
{
    InputTestApp test;
    test.Press(Key::Space, Scancode::Space);
    Fenrir::Input::Dispatch(test.app);
    Fenrir::Input::Dispatch(test.app);
    test.app.RunFrame(oneTick);

    Fenrir::EventReader<Fenrir::KeyEvent> reader;
    INFO("input dispatched by hand, then by the frame's dispatch, is sent once");
    REQUIRE(reader.Read(test.app).size() == 1);
    INFO("and the key is held");
    REQUIRE(test.Keyboard().IsDown(Key::Space));
}

TEST_CASE("Dispatching an app with no input installed does nothing", "[input][dispatch]")
{
    Fenrir::App app(std::make_unique<SilentLogger>(), 1);
    Fenrir::Input::Dispatch(app);

    INFO("the app gains no input resources");
    REQUIRE(app.TryGetResource<Fenrir::Keyboard>() == nullptr);
}
