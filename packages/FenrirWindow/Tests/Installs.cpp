// Window::Install: starts SDL, makes no window until the first frame, shares FenrirInput, and fails softly
// (spec: "Engine prerequisites", "FenrirWindow").

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "FenrirInput/Input.hpp"
#include "WindowTestApp.hpp"

using Fenrir::Window;
using namespace WindowTests;

TEST_CASE("When SDL's video cannot start, Install logs why and returns an invalid owner", "[window][install][no-video]")
{
    WindowTestApp test;

    INFO("the owner is not valid");
    REQUIRE_FALSE(test.window.IsValid());
    INFO("one diagnostic says why");
    REQUIRE(test.errors->size() == 1);
    REQUIRE(test.errors->front().find("FenrirWindow") != std::string::npos);

    test.app.RunFrame(oneTick);
    INFO("the app keeps running, with no window made and none of FenrirWindow's functions doing anything");
    REQUIRE(test.app.IsRunning());
    REQUIRE_FALSE(Window::Exists(test.app, Window::MainWindow()));
    REQUIRE(Window::LogicalSize(test.app, Window::MainWindow()) == Fenrir::WindowSize{});
    REQUIRE_FALSE(Window::SetTitle(test.app, Window::MainWindow(), "ignored"));
}

TEST_CASE("Install makes no window until the first frame's PreInit", "[window][install][display]")
{
    WindowTestApp test;
    test.RequireDisplay();

    INFO("after Install, the main window does not exist yet");
    REQUIRE_FALSE(Window::Exists(test.app, Window::MainWindow()));

    test.app.RunFrame(oneTick);
    INFO("after the first frame it exists, at the configured logical size");
    REQUIRE(Window::Exists(test.app, Window::MainWindow()));
    REQUIRE(Window::LogicalSize(test.app, Window::MainWindow()) == Fenrir::WindowSize{320, 240});
}

TEST_CASE("Install adds FenrirInput when the app has none, and shares it when it has", "[window][install][display]")
{
    SECTION("installed by the window")
    {
        WindowTestApp test;
        test.RequireDisplay();

        INFO("the app has the input resources without installing input itself");
        REQUIRE(test.app.TryGetResource<Fenrir::Keyboard>() != nullptr);
        REQUIRE(test.app.TryGetResource<Fenrir::InputFeed>() != nullptr);
    }
    SECTION("installed by the game first")
    {
        auto errors = std::make_shared<std::vector<std::string>>();
        Fenrir::App app(std::make_unique<RecordingLogger>(errors), 1);
        std::optional<Fenrir::Input> input = Fenrir::Input::Install(app);
        std::optional<Window> window = Window::Install(app, HiddenWindow());
        if (!window->IsValid())
        {
            SKIP("SDL's video could not start (no display), so no window can be made");
        }

        input.reset();
        INFO("the game's input owner going leaves the input in place while the window lives");
        REQUIRE(app.TryGetResource<Fenrir::Keyboard>() != nullptr);
        window.reset();
        INFO("once the window goes too, the input goes with its last owner");
        REQUIRE(app.TryGetResource<Fenrir::Keyboard>() == nullptr);
    }
}

TEST_CASE("A second Install while a Window lives logs and returns an invalid owner", "[window][install][display]")
{
    WindowTestApp test;
    test.RequireDisplay();

    Window second = Window::Install(test.app, HiddenWindow());
    INFO("the second owner is not valid, and one diagnostic says why");
    REQUIRE_FALSE(second.IsValid());
    REQUIRE(test.errors->size() == 1);

    test.app.RunFrame(oneTick);
    INFO("the first Window still works: its main window is made");
    REQUIRE(Window::Exists(test.app, Window::MainWindow()));
}

TEST_CASE("A moved Window owner keeps the window, and destroying it ends FenrirWindow", "[window][install][display]")
{
    WindowTestApp test;
    test.RequireDisplay();
    test.app.RunFrame(oneTick);

    std::optional<Window> moved = std::move(test.window);
    INFO("the moved-from owner is not valid, the one moved to is, and the window is still there");
    REQUIRE_FALSE(test.window.IsValid());
    REQUIRE(moved->IsValid());
    REQUIRE(Window::Exists(test.app, Window::MainWindow()));

    moved.reset();
    INFO("once the owner is destroyed, the window is gone");
    REQUIRE_FALSE(Window::Exists(test.app, Window::MainWindow()));

    test.app.RunFrame(oneTick);
    Window again = Window::Install(test.app, HiddenWindow());
    INFO("FenrirWindow can be installed again afterwards");
    REQUIRE(again.IsValid());
}
