// The window from C# through the scripting fixture harness (spec seam 7): with no window every Fenrir.Window function
// no-ops with its documented return and one warning per native call site; with a hidden window (skipped without a
// display) C# reads what C++ does, round-trips the clipboard and reads the window's events. C++ calls the fixture's
// static methods through Iris, as the system runner calls scripts.

#include "HeadlessApp.hpp"
#include "ScriptingProbe.hpp"
#include "WindowProbe.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include "FenrirInputScripting/InputScripting.hpp"
#include "FenrirScripting/Scripting.hpp"
#include "FenrirWindow/Window.hpp"
#include "FenrirWindowScripting/WindowScripting.hpp"
#include "Utf16.hpp"

using namespace Fenrir;

namespace
{
    std::filesystem::path FromEnvironment(const char* name)
    {
        const char* value = std::getenv(name);
        if (value == nullptr)
        {
            std::cerr << "Set " << name << " (CTest does): the window scripting tests need the fixture assembly.\n";
            return {};
        }
        return std::filesystem::absolute(value);
    }

    const std::filesystem::path fixture = FromEnvironment("FENRIR_TEST_WINDOW_FIXTURE");

    constexpr double oneTick = Testing::HeadlessApp::FrameDelta;
    constexpr const char* CallsType = "WindowScriptFixture.WindowCalls";
    constexpr const char* EventsType = "WindowScriptFixture.WindowEvents";

    // Calls a static method of a type in the loaded fixture through Iris.
    template <typename Result, typename... Args>
    Result Call(Scripting& scripting, const char* typeName, const char* method, Args... arguments)
    {
        auto* component = ScriptingProbe::Component(scripting);
        {
            INFO("There is no loaded script assembly");
            REQUIRE(component != nullptr);
        }
        const auto type = component->FindType(typeName);
        {
            INFO(std::string("The fixture type was not found: ") + type.diagnostic);
            REQUIRE(type.Succeeded());
        }
        const auto handle = type.value->FindMethod<Result(Args...)>(method);
        {
            INFO(std::string("The fixture method ") + method + " was not found: " + handle.diagnostic);
            REQUIRE(handle.Succeeded());
        }
        const auto result = handle.value->Invoke(arguments...);
        {
            INFO(std::string("The fixture method ") + method + " failed: " + result.diagnostic);
            REQUIRE(result.Succeeded());
        }
        if constexpr (!std::is_void_v<Result>)
        {
            return *result.value;
        }
    }

    // Scripting running the fixture with input's and the window's extensions, in that order.
    ScriptingConfig FixtureConfig()
    {
        ScriptingConfig config;
        config.scriptAssembly = fixture;
        config.hotReload = false;
        config.extensions = {Input::ScriptingExtension(), Window::ScriptingExtension()};
        return config;
    }

    std::string Text(Scripting& scripting, const char* method, int capacity)
    {
        return Call<std::optional<std::string>>(scripting, CallsType, method, capacity).value_or("<null>");
    }

    // How many warnings the app logged that contain `text`.
    std::size_t WarningsWith(const Testing::HeadlessApp& headless, const std::string& text)
    {
        std::size_t count = 0;
        for (const Testing::LoggedMessage& message : headless.Logger().Messages())
        {
            count += message.level == Testing::LogLevel::Warn && message.text.find(text) != std::string::npos ? 1 : 0;
        }
        return count;
    }

    // A hidden main window, as GL tests use.
    WindowConfig HiddenWindow()
    {
        WindowConfig config;
        config.title = "FenrirWindowScriptingTests";
        config.logicalSize = {320, 240};
        config.visible = false;
        return config;
    }

    // An App with a hidden window and scripting running the fixture. Members end in reverse order: scripting, then the
    // window, then the app.
    struct WindowScriptingApp
    {
        WindowScriptingApp()
            : window(Window::Install(headless.Get(), HiddenWindow())),
              scripting(Scripting::Install(headless.Get(), FixtureConfig()))
        {
            INFO("The fixture did not load with input's and the window's extensions");
            REQUIRE(scripting.HasScripts());
        }

        Testing::HeadlessApp headless;
        Window window;
        Scripting scripting;

        App& Get()
        {
            return headless.Get();
        }

        // Skips the calling test when SDL's video could not start: no display to make a window on.
        void RequireDisplay() const
        {
            if (!window.IsValid())
            {
                SKIP("SDL's video could not start (no display), so no window can be made");
            }
        }

        template <typename Result, typename... Args>
        Result Call(const char* typeName, const char* method, Args... arguments)
        {
            return ::Call<Result>(scripting, typeName, method, arguments...);
        }
    };

    // A release build warns about nothing; a debug build once per native call site.
#ifdef NDEBUG
    constexpr std::size_t warningsPerCallSite = 0;
#else
    constexpr std::size_t warningsPerCallSite = 1;
#endif

    constexpr std::uint32_t mainWindow = 1;
    constexpr std::uint32_t noWindow = 99;
} // namespace

TEST_CASE("With no window, every C# window function does nothing and warns once per native call site",
          "[window][scripting]")
{
    // Scripting with both extensions, but no Window::Install: scripts still load.
    Testing::HeadlessApp headless;
    Scripting scripting = Scripting::Install(headless.Get(), FixtureConfig());
    {
        INFO("The fixture did not load with the window's extension and no window");
        REQUIRE(scripting.HasScripts());
    }

    {
        INFO("C#'s Window.Main is not FenrirWindow's main window");
        REQUIRE(Call<std::uint32_t>(scripting, CallsType, "Main") == Window::MainWindow().value);
    }
    for (int call = 0; call < 2; ++call)
    {
        INFO("Call " << call << ": a function did something, or returned other than false, zero or empty");
        REQUIRE_FALSE(Call<bool>(scripting, CallsType, "Exists", mainWindow));
        REQUIRE(Call<int>(scripting, CallsType, "Width", mainWindow) == 0);
        REQUIRE(Call<int>(scripting, CallsType, "Height", mainWindow) == 0);
        REQUIRE(Call<int>(scripting, CallsType, "PixelWidth", mainWindow) == 0);
        REQUIRE(Call<int>(scripting, CallsType, "PixelHeight", mainWindow) == 0);
        REQUIRE(Call<float>(scripting, CallsType, "ContentScale", mainWindow) == 0.0f);
        REQUIRE_FALSE(Call<bool>(scripting, CallsType, "IsMinimised", mainWindow));
        REQUIRE_FALSE(Call<bool>(scripting, CallsType, "IsFocused", mainWindow));
        REQUIRE(Call<int>(scripting, CallsType, "GetCursorMode", mainWindow) == static_cast<int>(CursorMode::Normal));
        REQUIRE_FALSE(
            Call<bool>(scripting, CallsType, "SetCursorMode", mainWindow, static_cast<int>(CursorMode::Captured)));
        REQUIRE_FALSE(
            Call<bool>(scripting, CallsType, "SetCursorShape", mainWindow, static_cast<int>(CursorShape::Hand)));
        REQUIRE_FALSE(Call<bool>(scripting, CallsType, "SetTitle", mainWindow, std::string_view("Title")));
        REQUIRE_FALSE(Call<bool>(scripting, CallsType, "SetClipboard", std::string_view("Clipboard")));
        REQUIRE(Call<int>(scripting, CallsType, "ClipboardLength", 16) == 0);
        REQUIRE(Text(scripting, "Clipboard", 16).empty());
        REQUIRE_FALSE(Call<bool>(scripting, CallsType, "RequestClose", mainWindow));
    }
    {
        INFO("Nothing stopped the app");
        REQUIRE(headless.Get().IsRunning());
    }

    for (const char* function :
         {"Window.Size", "Window.PixelSize", "Window.ContentScale", "Window.IsMinimised", "Window.IsFocused",
          "Window.GetCursorMode", "Window.SetCursorMode", "Window.SetCursorShape", "Window.SetTitle",
          "Window.SetClipboard", "Window.GetClipboard", "Window.RequestClose"})
    {
        INFO(function << " did not warn once in a debug build (never in a release build), though called at least "
                         "twice");
        REQUIRE(WarningsWith(headless, std::string(function) + ": the app has no such window") == warningsPerCallSite);
    }
}

TEST_CASE("The C# WindowSize has the C++ layout", "[window][scripting]")
{
    Testing::HeadlessApp headless;
    Scripting scripting = Scripting::Install(headless.Get(), FixtureConfig());
    REQUIRE(scripting.HasScripts());
    // The events holding it are checked field by field, offsets included, as the fixture loads: it loaded.
    INFO("C# WindowSize is not the size of C++'s");
    REQUIRE(Call<int>(scripting, CallsType, "WindowSizeSize") == static_cast<int>(sizeof(WindowSize)));
}

TEST_CASE("UTF-8 becomes the UTF-16 C# reads, with U+FFFD for bytes that are not UTF-8", "[window][scripting]")
{
    {
        INFO("ASCII, two-, three- and four-byte forms (the last as a surrogate pair) did not convert");
        REQUIRE(Detail::Utf8ToUtf16("A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80") == u"Aé€\U0001F600");
    }
    INFO("A byte that starts no valid form (a stray continuation byte, a cut-short form, an overlong form, an encoded "
         "surrogate) did not become U+FFFD, with decoding going on from the next byte");
    REQUIRE(Detail::Utf8ToUtf16("\x80") == u"�");
    REQUIRE(Detail::Utf8ToUtf16("a\xE2\x82") == u"a��");
    REQUIRE(Detail::Utf8ToUtf16("\xC0\xAF") == u"��");
    REQUIRE(Detail::Utf8ToUtf16("\xED\xA0\x80") == u"���");
}

TEST_CASE("C# reads a hidden window's sizes, focus and cursor as C++ does", "[window][scripting][display]")
{
    WindowScriptingApp test;
    test.RequireDisplay();
    test.Get().RunFrame(oneTick);
    const WindowId main = Window::MainWindow();

    {
        INFO("C# does not see the main window made, at 320 x 240 logical units as C++ does");
        REQUIRE(test.Call<bool>(CallsType, "Exists", mainWindow));
        REQUIRE(test.Call<int>(CallsType, "Width", mainWindow) == 320);
        REQUIRE(test.Call<int>(CallsType, "Height", mainWindow) == 240);
        REQUIRE(Window::LogicalSize(test.Get(), main) == WindowSize{320, 240});
    }
    {
        const WindowSize pixels = Window::PixelSize(test.Get(), main);
        INFO("C# does not read the pixel size, content scale, minimised and focus C++ does");
        REQUIRE(test.Call<int>(CallsType, "PixelWidth", mainWindow) == pixels.width);
        REQUIRE(test.Call<int>(CallsType, "PixelHeight", mainWindow) == pixels.height);
        REQUIRE(test.Call<float>(CallsType, "ContentScale", mainWindow) > 0.0f);
        REQUIRE_FALSE(test.Call<float>(CallsType, "ContentScale", mainWindow) < Window::ContentScale(test.Get(), main));
        REQUIRE_FALSE(test.Call<float>(CallsType, "ContentScale", mainWindow) > Window::ContentScale(test.Get(), main));
        REQUIRE(test.Call<bool>(CallsType, "IsMinimised", mainWindow) == Window::IsMinimised(test.Get(), main));
        REQUIRE(test.Call<bool>(CallsType, "IsFocused", mainWindow) == Window::IsFocused(test.Get(), main));
    }
    {
        INFO("A cursor mode set from C# is not the one C++ and C# read back");
        REQUIRE(test.Call<bool>(CallsType, "SetCursorMode", mainWindow, static_cast<int>(CursorMode::Hidden)));
        REQUIRE(Window::GetCursorMode(test.Get(), main) == CursorMode::Hidden);
        REQUIRE(test.Call<int>(CallsType, "GetCursorMode", mainWindow) == static_cast<int>(CursorMode::Hidden));
        REQUIRE(test.Call<bool>(CallsType, "SetCursorMode", mainWindow, static_cast<int>(CursorMode::Normal)));
    }
    {
        INFO("C# could not set the cursor shape and the title");
        REQUIRE(test.Call<bool>(CallsType, "SetCursorShape", mainWindow, static_cast<int>(CursorShape::Text)));
        REQUIRE(test.Call<bool>(CallsType, "SetTitle", mainWindow, std::string_view("Titled from C# \xC3\xA9")));
    }
    INFO("A window id that names no window reads as none, and takes nothing");
    REQUIRE_FALSE(test.Call<bool>(CallsType, "Exists", noWindow));
    REQUIRE(test.Call<int>(CallsType, "Width", noWindow) == 0);
    REQUIRE_FALSE(test.Call<bool>(CallsType, "SetTitle", noWindow, std::string_view("Nowhere")));
    REQUIRE_FALSE(test.Call<bool>(CallsType, "RequestClose", noWindow));
}

TEST_CASE("C# round-trips the clipboard, filling a span and returning the text's whole length",
          "[window][scripting][display]")
{
    WindowScriptingApp test;
    test.RequireDisplay();
    test.Get().RunFrame(oneTick);

    // With an e-acute, a euro sign and an emoji, in UTF-8: 11 UTF-16 chars, the emoji two of them.
    const std::string text = "Fenrir \xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";
    {
        INFO("C# could not put the text on the clipboard");
        REQUIRE(test.Call<bool>(CallsType, "SetClipboard", std::string_view(text)));
    }
    {
        INFO("C++ does not read back what C# put there");
        REQUIRE(Window::ClipboardText(test.Get()) == text);
    }
    {
        INFO("C# does not read back the whole text into a span with room for it");
        REQUIRE(test.Call<int>(CallsType, "ClipboardLength", 64) == 11);
        REQUIRE(Text(test.scripting, "Clipboard", 64) == text);
    }
    INFO("Into a span of 4 chars C# does not get the first 4, with the whole length returned");
    REQUIRE(test.Call<int>(CallsType, "ClipboardLength", 4) == 11);
    REQUIRE(Text(test.scripting, "Clipboard", 4) == "Fenr");
}

TEST_CASE("C# reads the window's resize, focus and close-requested events, and asks for the close",
          "[window][scripting][display]")
{
    WindowScriptingApp test;
    test.RequireDisplay();
    test.Get().RunFrame(oneTick);
    test.Call<void>(EventsType, "Clear");

    {
        INFO("SDL did not resize the hidden window");
        REQUIRE(WindowProbe::Resize(test.Get(), Window::MainWindow(), WindowSize{400, 300}));
    }
    WindowProbe::PushFocusGained(test.Get(), Window::MainWindow());
    test.Get().RunFrame(oneTick);
    {
        INFO("C# could not ask for the main window's close");
        REQUIRE(test.Call<bool>(CallsType, "RequestClose", mainWindow));
    }
    {
        INFO("The close request from C# did not stop the app, as the close button does");
        REQUIRE_FALSE(test.Get().IsRunning());
    }
    test.Get().RunFrame(oneTick);

    const auto events = Call<std::optional<std::string>>(test.scripting, EventsType, "Log").value_or("");
    INFO("C# EventReaders did not read the resize, the focus and the close request once each; they read:\n" << events);
    REQUIRE(events == "Resized 1 400x300\n"
                      "Focus 1 1\n"
                      "CloseRequested 1");
}
