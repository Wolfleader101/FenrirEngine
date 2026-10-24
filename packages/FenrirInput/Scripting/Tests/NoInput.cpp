// The no-op rule (spec: "Scripting: the extensions"): with input's extension but no input installed, scripts still
// load, and every Fenrir.Input function does nothing, returns false or zero, and warns once per native call site.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>

#include "InputScriptingApp.hpp"

using namespace Fenrir;
using namespace InputScriptingTests;

namespace
{
    // A release build warns about nothing; a debug build once per native call site.
#ifdef NDEBUG
    constexpr std::size_t warningsPerCallSite = 0;
#else
    constexpr std::size_t warningsPerCallSite = 1;
#endif
} // namespace

TEST_CASE("With no input installed, the C# facades read nothing and warn once per native call site",
          "[input][scripting]")
{
    Testing::HeadlessApp headless;
    Scripting scripting = Scripting::Install(headless.Get(), FixtureConfig());
    {
        INFO("The fixture did not load with input's extension and no input");
        REQUIRE(scripting.HasScripts());
    }
    // No frame runs, so the fixture's systems (which read the keyboard too, from call sites of their own) stay quiet.

    for (int call = 0; call < 2; ++call)
    {
        INFO("Call " << call << ": a read did not give false or zero");
        REQUIRE_FALSE(Call<bool>(scripting, KeysType, "KeyIsDown", static_cast<int>(Key::Space)));
        REQUIRE_FALSE(Call<bool>(scripting, KeysType, "ScancodeJustPressed", static_cast<int>(Scancode::Space)));
        REQUIRE_FALSE(Call<bool>(scripting, KeysType, "ScancodeJustReleased", static_cast<int>(Scancode::Space)));
        REQUIRE(Call<int>(scripting, KeysType, "Modifiers") == 0);
        REQUIRE_FALSE(Call<bool>(scripting, DevicesType, "MouseIsDown", 0));
        REQUIRE(Call<float>(scripting, DevicesType, "MousePosition", 0) == 0.0f);
        REQUIRE(Call<float>(scripting, DevicesType, "MouseMotion", 1) == 0.0f);
        REQUIRE(Call<float>(scripting, DevicesType, "MouseScroll", 1) == 0.0f);
        REQUIRE(Call<int>(scripting, DevicesType, "ConnectedCount") == 0);
        REQUIRE_FALSE(Call<bool>(scripting, DevicesType, "GamepadIsConnected", 1u));
        REQUIRE_FALSE(Call<bool>(scripting, DevicesType, "GamepadIsDown", 1u, 0));
        REQUIRE(Call<float>(scripting, DevicesType, "GamepadAxis", 1u, 0) == 0.0f);
        REQUIRE_FALSE(Call<bool>(scripting, DevicesType, "HasClaims"));
    }

    for (const char* function : {"Keyboard.IsDown", "Keyboard.JustPressed", "Keyboard.JustReleased",
                                 "Keyboard.Modifiers", "Mouse.IsDown", "Mouse.Position", "Mouse.Motion", "Mouse.Scroll",
                                 "Gamepads.Connected", "Gamepads.IsConnected", "Gamepads.IsDown", "Gamepads.Axis"})
    {
        INFO(function << " did not warn once in a debug build (never in a release build), though called twice");
        REQUIRE(WarningsWith(headless, std::string(function) + ": the app has no input") == warningsPerCallSite);
    }
}
