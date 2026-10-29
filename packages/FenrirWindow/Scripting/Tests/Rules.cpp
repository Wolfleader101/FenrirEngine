// The window natives' rules, with no .NET and no fixture assembly: through NativeCallHarness, which runs the window
// extension as shipped (Window::ScriptingExtension()), a native with no window of its id returns its default and warns
// once under its name, an unknown cursor mode or shape does nothing and warns once, and the clipboard is written up to
// a capacity with its whole length returned. Each native is called by its registered name through the registration's
// own thunk, which zeroes each single output before the native runs; the warning texts are WindowScripting's.

#include "HeadlessApp.hpp"
#include "NativeCallHarness.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string_view>

#include "FenrirInput/InputTypes.hpp"
#include "FenrirWindow/Window.hpp"
#include "FenrirWindow/WindowTypes.hpp"
#include "FenrirWindowScripting/WindowScripting.hpp"
#include "WindowScriptingState.hpp"

namespace
{
    using namespace Fenrir;
    using namespace std::string_view_literals;

    // A sentinel an output is set to before a call, to show the registration zeroed it.
    constexpr WindowSize Dirty{7, 9};

    // A hidden main window, as GL tests use.
    WindowConfig HiddenWindow()
    {
        WindowConfig config;
        config.title = "FenrirWindowScriptingRules";
        config.logicalSize = {320, 240};
        config.visible = false;
        return config;
    }
} // namespace

TEST_CASE("With no window, each window native returns its default and warns once under its name across two calls",
          "[window][scripting][rules]")
{
    // A HeadlessApp with no Window::Install: the extension needs no owner, so its natives are called as C# would.
    Testing::NativeCallHarness harness({Window::ScriptingExtension()});
    const WindowId main = Window::MainWindow();

    for (int call = 0; call < 2; ++call)
    {
        INFO("Call " << call << ": a native did something, or returned other than false, zero or empty");
        WindowSize size = Dirty;
        harness.Call<void>("WindowNative.Size", main, &size);
        REQUIRE(size == WindowSize{});
        size = Dirty;
        harness.Call<void>("WindowNative.PixelSize", main, &size);
        REQUIRE(size == WindowSize{});
        REQUIRE(harness.Call<float>("WindowNative.ContentScale", main) == 0.0f);
        REQUIRE_FALSE(harness.Call<bool>("WindowNative.IsMinimised", main));
        REQUIRE_FALSE(harness.Call<bool>("WindowNative.IsFocused", main));
        REQUIRE(harness.Call<CursorMode>("WindowNative.GetCursorMode", main) == CursorMode::Normal);
        REQUIRE_FALSE(harness.Call<bool>("WindowNative.SetCursorMode", main, CursorMode::Captured));
        REQUIRE_FALSE(harness.Call<bool>("WindowNative.SetCursorShape", main, CursorShape::Hand));
        REQUIRE_FALSE(harness.Call<bool>("WindowNative.SetTitle", main, "Title"sv));
        REQUIRE_FALSE(harness.Call<bool>("WindowNative.SetClipboard", "Clipboard"sv));
        char16_t text[16] = {u'x'};
        REQUIRE(harness.Call<std::int32_t>("WindowNative.GetClipboard", text, 16) == 0);
        REQUIRE(text[0] == u'x');
        REQUIRE_FALSE(harness.Call<bool>("WindowNative.RequestClose", main));
    }
    {
        INFO("Nothing stopped the app");
        REQUIRE(harness.GetApp().IsRunning());
    }

    for (const char* native : {"WindowNative.Size", "WindowNative.PixelSize", "WindowNative.ContentScale",
                               "WindowNative.IsMinimised", "WindowNative.IsFocused", "WindowNative.GetCursorMode",
                               "WindowNative.SetCursorMode", "WindowNative.SetCursorShape", "WindowNative.SetTitle",
                               "WindowNative.SetClipboard", "WindowNative.GetClipboard", "WindowNative.RequestClose"})
    {
        INFO(native << " did not warn once in a debug build (never in a release build) under its name, though called "
                       "twice");
        REQUIRE(harness.WarnedOnce(native, WindowScripting::NoWindow));
    }
}

TEST_CASE("An unknown cursor mode or shape does nothing and warns once under the native's name",
          "[window][scripting][rules]")
{
    Testing::NativeCallHarness harness({Window::ScriptingExtension()});
    const WindowId main = Window::MainWindow();
    // One past the last enumerator of each, as a C# cast of a stray integer would arrive.
    const auto noMode = static_cast<CursorMode>(static_cast<int>(CursorMode::Captured) + 1);
    const auto noShape = static_cast<CursorShape>(static_cast<int>(CursorShape::Hidden) + 1);

    for (int call = 0; call < 2; ++call)
    {
        INFO("Call " << call << ": an unknown cursor mode or shape was taken");
        REQUIRE_FALSE(harness.Call<bool>("WindowNative.SetCursorMode", main, noMode));
        REQUIRE_FALSE(harness.Call<bool>("WindowNative.SetCursorShape", main, noShape));
    }
    {
        INFO("The unknown mode did not warn once, under the native's name, before any window check");
        REQUIRE(harness.WarnedOnce("WindowNative.SetCursorMode", WindowScripting::NoCursorMode));
        REQUIRE(harness.Warnings("WindowNative.SetCursorMode", WindowScripting::NoWindow) == 0);
    }
    INFO("The unknown shape did not warn once, under the native's name, before any window check");
    REQUIRE(harness.WarnedOnce("WindowNative.SetCursorShape", WindowScripting::NoCursorShape));
    REQUIRE(harness.Warnings("WindowNative.SetCursorShape", WindowScripting::NoWindow) == 0);
}

TEST_CASE("GetClipboard fills as many chars as the capacity holds and returns the text's whole length",
          "[window][scripting][rules][display]")
{
    // The clipboard is the platform's, so this needs the main window made: skipped where SDL's video cannot start.
    Testing::NativeCallHarness harness({Window::ScriptingExtension()});
    const Window window = Window::Install(harness.GetApp(), HiddenWindow());
    if (!window.IsValid())
    {
        SKIP("SDL's video could not start (no display), so no window can be made");
    }
    harness.GetApp().RunFrame(Testing::HeadlessApp::FrameDelta);

    // An e-acute and an emoji, in UTF-8: 5 UTF-16 chars, the emoji two of them.
    const std::string_view text = "A\xC3\xA9 \xF0\x9F\x98\x80";
    {
        INFO("The text could not be put on the clipboard");
        REQUIRE(harness.Call<bool>("WindowNative.SetClipboard", text));
    }
    {
        char16_t room[8] = {};
        INFO("With room for it, the whole text was not written and its length returned");
        REQUIRE(harness.Call<std::int32_t>("WindowNative.GetClipboard", room, 8) == 5);
        REQUIRE(std::u16string_view(room, 5) == u"Aé \U0001F600");
    }
    char16_t two[3] = {u'-', u'-', u'-'};
    INFO("With a capacity of two chars, the first two were not written, with the whole length returned and nothing "
         "past the capacity touched");
    REQUIRE(harness.Call<std::int32_t>("WindowNative.GetClipboard", two, 2) == 5);
    REQUIRE(std::u16string_view(two, 3) == u"Aé-");
    {
        INFO("The clipboard natives warned with the main window made");
        REQUIRE_FALSE(harness.Logger().Contains(Testing::LogLevel::Warn, "[window scripting]"));
    }
}
