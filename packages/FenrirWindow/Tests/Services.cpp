// The window's services on a hidden window: clipboard, title, icon and cursor (spec: "FenrirWindow", Services).

#include <cstddef>
#include <string>
#include <vector>

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

TEST_CASE("Setting the icon of a hidden window takes RGBA8 pixels of its size", "[window][services][display]")
{
    WindowTestApp test;
    test.RequireDisplay();
    const Fenrir::WindowId main = Window::MainWindow();
    // A 2x2 icon: red, green, blue and see-through white, four bytes each.
    const std::vector<std::byte> pixels = [] {
        std::vector<std::byte> bytes;
        for (const int value : {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 0})
        {
            bytes.push_back(static_cast<std::byte>(value));
        }
        return bytes;
    }();

    INFO("an icon set before the window exists is kept for it");
    REQUIRE(Window::SetIcon(test.app, main, 2, 2, pixels));
    test.app.RunFrame(oneTick);
    INFO("making the window with the kept icon logged nothing");
    REQUIRE(test.errors->empty());
    INFO("an icon set on the made window is taken by SDL");
    REQUIRE(Window::SetIcon(test.app, main, 2, 2, pixels));
    INFO("and logs nothing");
    REQUIRE(test.errors->empty());

    INFO("pixels that are not width * height * 4 bytes are refused");
    REQUIRE_FALSE(Window::SetIcon(test.app, main, 3, 2, pixels));
    INFO("with one diagnostic");
    REQUIRE(test.errors->size() == 1);
    INFO("an empty size is refused");
    REQUIRE_FALSE(Window::SetIcon(test.app, main, 0, 0, {}));
    INFO("with one more diagnostic");
    REQUIRE(test.errors->size() == 2);
    INFO("a window that does not exist has no icon to set");
    REQUIRE_FALSE(Window::SetIcon(test.app, Fenrir::WindowId{99}, 2, 2, pixels));
    INFO("and that is logged once too");
    REQUIRE(test.errors->size() == 3);
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

TEST_CASE("A window's cursor mode reads back as it was last set", "[window][services][display]")
{
    WindowTestApp test;
    test.RequireDisplay();
    const Fenrir::WindowId main = Window::MainWindow();
    INFO("the cursor starts in the normal mode");
    REQUIRE(Window::GetCursorMode(test.app, main) == CursorMode::Normal);
    test.app.RunFrame(oneTick);

    for (const CursorMode mode : {CursorMode::Captured, CursorMode::Hidden, CursorMode::Normal})
    {
        REQUIRE(Window::SetCursorMode(test.app, main, mode));
        INFO("cursor mode " << static_cast<int>(mode) << " reads back as set");
        REQUIRE(Window::GetCursorMode(test.app, main) == mode);
    }
    INFO("a window that does not exist reads as normal");
    REQUIRE(Window::GetCursorMode(test.app, Fenrir::WindowId{99}) == CursorMode::Normal);
}
