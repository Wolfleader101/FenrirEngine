// The window's services on a hidden window: clipboard, title and cursor (spec: "FenrirWindow", Services).

#include <string>

#include <catch2/catch_test_macros.hpp>

#include "WindowTestApp.hpp"

using Fenrir::CursorMode;
using Fenrir::CursorShape;
using Fenrir::Window;
using namespace WindowTests;

TEST_CASE("Clipboard text round-trips", "[window][services][display]")
{
    WindowTestApp test;
    test.RequireDisplay();
    test.app.RunFrame(oneTick);

    const std::string text = "Fenrir clipboard \xC3\xA9\xE2\x82\xAC"; // with an e-acute and a euro sign, in UTF-8
    INFO("SDL takes the text");
    REQUIRE(Window::SetClipboardText(test.app, text));
    INFO("and the clipboard gives the same text back");
    REQUIRE(Window::ClipboardText(test.app) == text);
}

TEST_CASE("Setting the title of a hidden window does not error", "[window][services][display]")
{
    WindowTestApp test;
    test.RequireDisplay();

    INFO("a title set before the window exists is kept for it");
    REQUIRE(Window::SetTitle(test.app, Window::MainWindow(), "Before"));
    test.app.RunFrame(oneTick);
    INFO("a title set on the made window is taken by SDL");
    REQUIRE(Window::SetTitle(test.app, Window::MainWindow(), "After"));
    INFO("a window that does not exist has no title to set");
    REQUIRE_FALSE(Window::SetTitle(test.app, Fenrir::WindowId{99}, "Nowhere"));
}

TEST_CASE("Every cursor mode and shape can be set on a hidden window", "[window][services][display]")
{
    WindowTestApp test;
    test.RequireDisplay();
    test.app.RunFrame(oneTick);
    const Fenrir::WindowId main = Window::MainWindow();

    for (const CursorMode mode : {CursorMode::Captured, CursorMode::Hidden, CursorMode::Normal})
    {
        INFO("cursor mode " << static_cast<int>(mode) << " is accepted");
        REQUIRE(Window::SetCursorMode(test.app, main, mode));
    }
    for (const CursorShape shape :
         {CursorShape::Text, CursorShape::ResizeEastWest, CursorShape::ResizeNorthSouth,
          CursorShape::ResizeNorthWestSouthEast, CursorShape::ResizeNorthEastSouthWest, CursorShape::Hand,
          CursorShape::NotAllowed, CursorShape::Hidden, CursorShape::Arrow})
    {
        INFO("cursor shape " << static_cast<int>(shape) << " is accepted");
        REQUIRE(Window::SetCursorShape(test.app, main, shape));
    }
    INFO("no diagnostic was logged");
    REQUIRE(test.errors->empty());
}
