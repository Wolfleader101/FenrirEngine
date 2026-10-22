// The main window: made hidden, resized and closed, each with its event (spec: "Testing Decisions", FenrirWindow).

#include <catch2/catch_test_macros.hpp>

#include "WindowProbe.hpp"
#include "WindowTestApp.hpp"

using Fenrir::Window;
using Fenrir::WindowSize;
using namespace WindowTests;

TEST_CASE("A hidden main window is made at its logical size, with a pixel size to match", "[window][display]")
{
    WindowTestApp test;
    test.RequireDisplay();
    test.app.RunFrame(oneTick);

    const Fenrir::WindowId main = Window::MainWindow();
    const float scale = Window::ContentScale(test.app, main);
    const WindowSize pixels = Window::PixelSize(test.app, main);
    INFO("the window exists at the configured 320 x 240 logical size");
    REQUIRE(Window::Exists(test.app, main));
    REQUIRE(Window::LogicalSize(test.app, main) == WindowSize{320, 240});
    INFO("its content scale is positive and its pixel size is non-zero (pixels: " << pixels.width << " x "
                                                                                  << pixels.height << ")");
    REQUIRE(scale > 0.0f);
    REQUIRE(pixels.width > 0);
    REQUIRE(pixels.height > 0);
    INFO("a hidden window is not minimised");
    REQUIRE_FALSE(Window::IsMinimised(test.app, main));
}

TEST_CASE("Resizing the main window sends a resize event with its new logical and pixel sizes", "[window][display]")
{
    WindowTestApp test;
    test.RequireDisplay();
    test.app.RunFrame(oneTick);
    const Fenrir::WindowId main = Window::MainWindow();
    const WindowSize pixelsBefore = Window::PixelSize(test.app, main);
    Fenrir::EventReader<Fenrir::WindowResizedEvent> reader;
    (void)ReadAll(reader, test.app);

    INFO("SDL resizes the hidden window");
    REQUIRE(Fenrir::WindowProbe::Resize(test.app, main, WindowSize{400, 300}));
    test.app.RunFrame(oneTick);

    const auto events = ReadAll(reader, test.app);
    INFO("one resize event names the main window");
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].window == main);
    INFO("it carries the new logical size, 400 x 300");
    REQUIRE(events[0].logicalSize == WindowSize{400, 300});
    INFO("and the new pixel size, which grew with it");
    REQUIRE(events[0].pixelSize.width > pixelsBefore.width);
    REQUIRE(events[0].pixelSize.height > pixelsBefore.height);
    INFO("the window reports the same sizes from then on");
    REQUIRE(Window::LogicalSize(test.app, main) == WindowSize{400, 300});
    REQUIRE(Window::PixelSize(test.app, main) == events[0].pixelSize);

    test.app.RunFrame(oneTick);
    INFO("a frame with no resize sends no resize event");
    REQUIRE(ReadAll(reader, test.app).size() == 0);
}

TEST_CASE("Closing the main window stops the app when stopOnMainWindowClose is set", "[window][display]")
{
    WindowTestApp test;
    test.RequireDisplay();
    test.app.RunFrame(oneTick);
    Fenrir::EventReader<Fenrir::WindowCloseRequestedEvent> reader;

    Fenrir::WindowProbe::PushCloseRequest(test.app, Window::MainWindow());
    test.app.RunFrame(oneTick);

    const auto events = ReadAll(reader, test.app);
    INFO("a close-requested event names the main window");
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].window == Window::MainWindow());
    INFO("the app is stopped");
    REQUIRE_FALSE(test.app.IsRunning());
    INFO("the window itself stays until teardown");
    REQUIRE(Window::Exists(test.app, Window::MainWindow()));
}

TEST_CASE("Without stopOnMainWindowClose, closing the main window leaves the app to the game", "[window][display]")
{
    Fenrir::WindowConfig config = HiddenWindow();
    config.stopOnMainWindowClose = false;
    WindowTestApp test(config);
    test.RequireDisplay();
    test.app.RunFrame(oneTick);
    Fenrir::EventReader<Fenrir::WindowCloseRequestedEvent> reader;

    Fenrir::WindowProbe::PushCloseRequest(test.app, Window::MainWindow());
    test.app.RunFrame(oneTick);

    INFO("the close-requested event is still sent, for game code to handle");
    REQUIRE(ReadAll(reader, test.app).size() == 1);
    INFO("the app keeps running");
    REQUIRE(test.app.IsRunning());
}

TEST_CASE("Requesting the main window's close acts as its close button", "[window][display]")
{
    WindowTestApp test;
    test.RequireDisplay();
    {
        INFO("a window not made yet cannot be asked to close");
        REQUIRE_FALSE(Window::RequestClose(test.app, Window::MainWindow()));
    }
    test.app.RunFrame(oneTick);
    Fenrir::EventReader<Fenrir::WindowCloseRequestedEvent> reader;

    {
        INFO("the made main window takes the request");
        REQUIRE(Window::RequestClose(test.app, Window::MainWindow()));
    }
    const auto events = ReadAll(reader, test.app);
    INFO("a close-requested event names the main window");
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].window == Window::MainWindow());
    INFO("with stopOnMainWindowClose the app is stopped, and the window stays");
    REQUIRE_FALSE(test.app.IsRunning());
    REQUIRE(Window::Exists(test.app, Window::MainWindow()));
    INFO("a window that does not exist cannot be asked to close");
    REQUIRE_FALSE(Window::RequestClose(test.app, Fenrir::WindowId{99}));
}

TEST_CASE("A hidden window that was never shown does not have the input focus", "[window][display]")
{
    WindowTestApp test;
    test.RequireDisplay();
    INFO("no window has the focus before it is made");
    REQUIRE_FALSE(Window::IsFocused(test.app, Window::MainWindow()));
    test.app.RunFrame(oneTick);
    INFO("a hidden window does not take the focus");
    REQUIRE_FALSE(Window::IsFocused(test.app, Window::MainWindow()));
    INFO("nor does a window that does not exist");
    REQUIRE_FALSE(Window::IsFocused(test.app, Fenrir::WindowId{99}));
}
