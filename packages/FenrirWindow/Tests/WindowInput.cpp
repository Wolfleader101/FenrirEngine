// What the pump takes from SDL reaches FenrirInput, and the game, in the same frame whichever package was installed
// first (spec: "FenrirWindow", settled 2026-10-22).

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirInput/Input.hpp"
#include "WindowProbe.hpp"
#include "WindowTestApp.hpp"

using Fenrir::Key;
using Fenrir::Scancode;
using Fenrir::Window;
using namespace WindowTests;

namespace
{
    // Records, in BeforeTicks of each frame, whether Space is held.
    struct HeldInBeforeTicks
    {
        std::shared_ptr<bool> held = std::make_shared<bool>(false);

        void AddTo(Fenrir::App& app) const
        {
            app.AddSequentialSystem(Fenrir::SchedulePriority::BeforeTicks, [record = held](Fenrir::App& running) {
                *record = running.GetResource<Fenrir::Keyboard>().IsDown(Key::Space);
            });
        }
    };
} // namespace

TEST_CASE("A key from SDL reaches the game in the same frame when the window is installed first",
          "[window][input][display]")
{
    WindowTestApp test;
    test.RequireDisplay();
    Fenrir::Input input = Fenrir::Input::Install(test.app);
    HeldInBeforeTicks space;
    space.AddTo(test.app);
    test.app.RunFrame(oneTick);

    Fenrir::WindowProbe::PushKeyDown(test.app, Window::MainWindow(), Scancode::Space);
    test.app.RunFrame(oneTick);

    INFO("Space, pushed before the frame, is held in that frame's BeforeTicks");
    REQUIRE(*space.held);
    INFO("and is just pressed in that frame");
    REQUIRE(test.app.GetResource<Fenrir::Keyboard>().JustPressed(Scancode::Space));
}

TEST_CASE("A key from SDL reaches the game in the same frame when input is installed first", "[window][input][display]")
{
    auto errors = std::make_shared<std::vector<std::string>>();
    Fenrir::App app(std::make_unique<RecordingLogger>(errors), 1);
    Fenrir::Input input = Fenrir::Input::Install(app);
    Window window = Window::Install(app, HiddenWindow());
    if (!window.IsValid())
    {
        SKIP("SDL's video could not start (no display), so no window can be made");
    }
    HeldInBeforeTicks space;
    space.AddTo(app);
    app.RunFrame(oneTick);

    Fenrir::WindowProbe::PushKeyDown(app, Window::MainWindow(), Scancode::Space);
    app.RunFrame(oneTick);

    INFO("Space, pushed before the frame, is held in that frame's BeforeTicks");
    REQUIRE(*space.held);
    Fenrir::EventReader<Fenrir::KeyEvent> reader;
    const auto events = ReadAll(reader, app);
    INFO("its key event is sent once, naming the main window, with the key code and scancode SDL gave");
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].window == Window::MainWindow());
    REQUIRE(events[0].key == Key::Space);
    REQUIRE(events[0].scancode == Scancode::Space);
    REQUIRE(events[0].pressed);
}

TEST_CASE("A window gaining the focus is fed to FenrirInput", "[window][input][display]")
{
    WindowTestApp test;
    test.RequireDisplay();
    test.app.RunFrame(oneTick);
    Fenrir::EventReader<Fenrir::WindowFocusEvent> reader;

    Fenrir::WindowProbe::PushFocusGained(test.app, Window::MainWindow());
    test.app.RunFrame(oneTick);

    const auto events = ReadAll(reader, test.app);
    INFO("one focus event says the main window gained the focus");
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].window == Window::MainWindow());
    REQUIRE(events[0].focused);
    INFO("and the keyboard names it as the focused window");
    REQUIRE(test.app.GetResource<Fenrir::Keyboard>().FocusedWindow() == Window::MainWindow());
}
