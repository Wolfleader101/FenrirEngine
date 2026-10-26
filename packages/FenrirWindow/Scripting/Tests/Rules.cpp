// The window natives' rules, with no .NET and no fixture assembly: through NativeCallHarness, with a WindowScripting
// state the test makes, a native with no window of its id returns its default and warns once under its name, an
// unknown cursor mode or shape does nothing and warns once, and the clipboard is written up to a capacity with its
// whole length returned. The natives are called as the generated registration declares them
// (src/Generated/FenrirWindowRegistration.cpp), which zeroes each single output before the native runs.

#include "HeadlessApp.hpp"
#include "NativeCallHarness.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "FenrirInput/InputTypes.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirScripting/ZeroingOutputs.hpp"
#include "FenrirWindow/Window.hpp"
#include "FenrirWindow/WindowTypes.hpp"
#include "WindowScriptingState.hpp"

namespace Fenrir::WindowNatives
{
    void Size(const CallContext&, WindowScripting&, WindowId window, WindowSize* size);
    void PixelSize(const CallContext&, WindowScripting&, WindowId window, WindowSize* size);
    float ContentScale(const CallContext&, WindowScripting&, WindowId window);
    bool IsMinimised(const CallContext&, WindowScripting&, WindowId window);
    bool IsFocused(const CallContext&, WindowScripting&, WindowId window);
    CursorMode GetCursorMode(const CallContext&, WindowScripting&, WindowId window);
    bool SetCursorMode(const CallContext&, WindowScripting&, WindowId window, CursorMode mode);
    bool SetCursorShape(const CallContext&, WindowScripting&, WindowId window, CursorShape shape);
    bool SetTitle(const CallContext&, WindowScripting&, WindowId window, std::string_view title);
    std::int32_t GetClipboard(const CallContext&, WindowScripting&, char16_t* text, std::int32_t capacity);
    bool SetClipboard(const CallContext&, WindowScripting&, std::string_view text);
    bool RequestClose(const CallContext&, WindowScripting&, WindowId window);
} // namespace Fenrir::WindowNatives

namespace
{
    using namespace Fenrir;

    // A release build warns about nothing (WarnOnceRecord, ADR-0043); a debug build once per place and message.
#ifdef NDEBUG
    constexpr std::size_t once = 0;
#else
    constexpr std::size_t once = 1;
#endif

    // WindowScripting::Made's warning, whole, so Logged can end it as the record does.
    constexpr std::string_view NoSuchWindow =
        "the app has no such window (no Window installed, the window not made yet, or an id that names none), so it "
        "does nothing";

    // How many warnings the app logged that contain `text`.
    std::size_t Warnings(const Testing::NativeCallHarness& harness, const std::string& text)
    {
        const auto messages = harness.Logger().Messages();
        return static_cast<std::size_t>(
            std::count_if(messages.begin(), messages.end(), [&](const Testing::LoggedMessage& message) {
                return message.level == Testing::LogLevel::Warn && message.text.find(text) != std::string::npos;
            }));
    }

    // The warning the window extension logs for `message` in the native `native`, as WarnOnceRecord spells it.
    std::string Logged(std::string_view native, std::string_view message)
    {
        return "[window scripting] " + std::string(native) + ": " + std::string(message) + " (warned once)";
    }

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
    Testing::NativeCallHarness harness;
    WindowScripting windows;
    const WindowId main = Window::MainWindow();

    for (int call = 0; call < 2; ++call)
    {
        INFO("Call " << call << ": a native did something, or returned other than false, zero or empty");
        WindowSize size = Dirty;
        ZeroingOutputs<&WindowNatives::Size, 3>::Call(harness.Context("WindowNative.Size"), windows, main, &size);
        REQUIRE(size == WindowSize{});
        size = Dirty;
        ZeroingOutputs<&WindowNatives::PixelSize, 3>::Call(harness.Context("WindowNative.PixelSize"), windows, main,
                                                           &size);
        REQUIRE(size == WindowSize{});
        REQUIRE(WindowNatives::ContentScale(harness.Context("WindowNative.ContentScale"), windows, main) == 0.0f);
        REQUIRE_FALSE(WindowNatives::IsMinimised(harness.Context("WindowNative.IsMinimised"), windows, main));
        REQUIRE_FALSE(WindowNatives::IsFocused(harness.Context("WindowNative.IsFocused"), windows, main));
        REQUIRE(WindowNatives::GetCursorMode(harness.Context("WindowNative.GetCursorMode"), windows, main) ==
                CursorMode::Normal);
        REQUIRE_FALSE(WindowNatives::SetCursorMode(harness.Context("WindowNative.SetCursorMode"), windows, main,
                                                   CursorMode::Captured));
        REQUIRE_FALSE(WindowNatives::SetCursorShape(harness.Context("WindowNative.SetCursorShape"), windows, main,
                                                    CursorShape::Hand));
        REQUIRE_FALSE(WindowNatives::SetTitle(harness.Context("WindowNative.SetTitle"), windows, main, "Title"));
        REQUIRE_FALSE(WindowNatives::SetClipboard(harness.Context("WindowNative.SetClipboard"), windows, "Clipboard"));
        char16_t text[16] = {u'x'};
        REQUIRE(WindowNatives::GetClipboard(harness.Context("WindowNative.GetClipboard"), windows, text, 16) == 0);
        REQUIRE(text[0] == u'x');
        REQUIRE_FALSE(WindowNatives::RequestClose(harness.Context("WindowNative.RequestClose"), windows, main));
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
        REQUIRE(Warnings(harness, Logged(native, NoSuchWindow)) == once);
    }
}

TEST_CASE("An unknown cursor mode or shape does nothing and warns once under the native's name",
          "[window][scripting][rules]")
{
    Testing::NativeCallHarness harness;
    WindowScripting windows;
    const WindowId main = Window::MainWindow();
    // One past the last enumerator of each, as a C# cast of a stray integer would arrive.
    const auto noMode = static_cast<CursorMode>(static_cast<int>(CursorMode::Captured) + 1);
    const auto noShape = static_cast<CursorShape>(static_cast<int>(CursorShape::Hidden) + 1);

    for (int call = 0; call < 2; ++call)
    {
        INFO("Call " << call << ": an unknown cursor mode or shape was taken");
        REQUIRE_FALSE(
            WindowNatives::SetCursorMode(harness.Context("WindowNative.SetCursorMode"), windows, main, noMode));
        REQUIRE_FALSE(
            WindowNatives::SetCursorShape(harness.Context("WindowNative.SetCursorShape"), windows, main, noShape));
    }
    {
        INFO("The unknown mode did not warn once, under the native's name, before any window check");
        REQUIRE(Warnings(harness, Logged("WindowNative.SetCursorMode", "no such cursor mode, so it does nothing")) ==
                once);
        REQUIRE(Warnings(harness, Logged("WindowNative.SetCursorMode", NoSuchWindow)) == 0);
    }
    INFO("The unknown shape did not warn once, under the native's name, before any window check");
    REQUIRE(Warnings(harness, Logged("WindowNative.SetCursorShape", "no such cursor shape, so it does nothing")) ==
            once);
    REQUIRE(Warnings(harness, Logged("WindowNative.SetCursorShape", NoSuchWindow)) == 0);
}

TEST_CASE("GetClipboard fills as many chars as the capacity holds and returns the text's whole length",
          "[window][scripting][rules][display]")
{
    // The clipboard is the platform's, so this needs the main window made: skipped where SDL's video cannot start.
    Testing::NativeCallHarness harness;
    const Window window = Window::Install(harness.GetApp(), HiddenWindow());
    if (!window.IsValid())
    {
        SKIP("SDL's video could not start (no display), so no window can be made");
    }
    harness.GetApp().RunFrame(Testing::HeadlessApp::FrameDelta);
    WindowScripting windows;

    // An e-acute and an emoji, in UTF-8: 5 UTF-16 chars, the emoji two of them.
    const std::string_view text = "A\xC3\xA9 \xF0\x9F\x98\x80";
    {
        INFO("The text could not be put on the clipboard");
        REQUIRE(WindowNatives::SetClipboard(harness.Context("WindowNative.SetClipboard"), windows, text));
    }
    {
        char16_t room[8] = {};
        INFO("With room for it, the whole text was not written and its length returned");
        REQUIRE(WindowNatives::GetClipboard(harness.Context("WindowNative.GetClipboard"), windows, room, 8) == 5);
        REQUIRE(std::u16string_view(room, 5) == u"Aé \U0001F600");
    }
    char16_t two[3] = {u'-', u'-', u'-'};
    INFO("With a capacity of two chars, the first two were not written, with the whole length returned and nothing "
         "past the capacity touched");
    REQUIRE(WindowNatives::GetClipboard(harness.Context("WindowNative.GetClipboard"), windows, two, 2) == 5);
    REQUIRE(std::u16string_view(two, 3) == u"Aé-");
    REQUIRE(Warnings(harness, "[window scripting]") == 0);
}
