// The graphics surface functions, as far as a window without a GL context can check them (ADR-0022). Making a GL
// context, and presenting through it, is first exercised by the renderer's GL tests (ticket 10).

#include <string>

#include <catch2/catch_test_macros.hpp>

#include "WindowTestApp.hpp"

using Fenrir::Window;
using namespace WindowTests;

TEST_CASE("A window made without a surface offers none", "[window][surface][display]")
{
    WindowTestApp test;
    test.RequireDisplay();
    test.app.RunFrame(oneTick);
    const Fenrir::WindowId main = Window::MainWindow();

    INFO("the plain main window holds no surface");
    REQUIRE_FALSE(Window::HasSurface(test.app, main));
    INFO("so it has no proc addresses, cannot be made current, and refuses a swap interval");
    REQUIRE(Window::GetProcAddress(test.app, main, "glClear") == nullptr);
    REQUIRE_FALSE(Window::MakeCurrent(test.app, main));
    REQUIRE_FALSE(Window::SetSwapInterval(test.app, main, 1));
    Window::Present(test.app, main);
    Window::ReleaseSurface(test.app, main);
    INFO("presenting and releasing it do nothing, and log nothing");
    REQUIRE(test.errors->empty());
}

TEST_CASE("A surface cannot be requested for a window already made without one", "[window][surface][display]")
{
    WindowTestApp test;
    test.RequireDisplay();
    test.app.RunFrame(oneTick);

    std::string error;
    INFO("the request is refused");
    REQUIRE_FALSE(Window::RequestSurface(test.app, Window::MainWindow(), Fenrir::SurfaceRequest{}, error));
    INFO("with a message saying why");
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("A surface cannot be requested for a window FenrirWindow does not have", "[window][surface][display]")
{
    WindowTestApp test;
    test.RequireDisplay();

    std::string error;
    INFO("a request for an unknown window id is refused with a message");
    REQUIRE_FALSE(Window::RequestSurface(test.app, Fenrir::WindowId{99}, Fenrir::SurfaceRequest{}, error));
    REQUIRE_FALSE(error.empty());
    INFO("and the main window is still made in the first frame");
    test.app.RunFrame(oneTick);
    REQUIRE(Window::Exists(test.app, Window::MainWindow()));
}
