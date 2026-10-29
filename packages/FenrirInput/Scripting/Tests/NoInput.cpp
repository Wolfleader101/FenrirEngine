// The no-op rule (spec: "Scripting: the extensions"), with no .NET: with the input extension as shipped
// (Input::ScriptingExtension()) but no input installed in the app, every input native function does nothing, returns
// false or zero, and warns once per native function under its name, through NativeCallHarness (the native guard spec:
// "Rule tests move onto it"), which calls each native by its registered name through the registration's own thunk.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>

#include "FenrirApp/App.hpp"
#include "FenrirInput/Devices.hpp"
#include "FenrirInput/InputTypes.hpp"
#include "FenrirInputScripting/InputScripting.hpp"
#include "FenrirMath/Math.hpp"
#include "InputClaimsResource.hpp"
#include "InputScriptingState.hpp"
#include "NativeCallHarness.hpp"

namespace
{
    using namespace Fenrir;

    constexpr GamepadId pad{1};
} // namespace

TEST_CASE("With no input installed, every input native reads false or zero and warns once under its name",
          "[input][scripting][rules]")
{
    Testing::NativeCallHarness harness({Input::ScriptingExtension()});

    // Each native twice, from one place, so a second call shows the warning was given once.
    for (int call = 0; call < 2; ++call)
    {
        INFO("Call " << call << ": a read did not give false or zero");
        REQUIRE_FALSE(harness.Call<bool>("InputNative.KeyIsDown", Key::Space));
        REQUIRE_FALSE(harness.Call<bool>("InputNative.ScancodeIsDown", Scancode::Space));
        REQUIRE_FALSE(harness.Call<bool>("InputNative.KeyJustPressed", Key::Space));
        REQUIRE_FALSE(harness.Call<bool>("InputNative.ScancodeJustPressed", Scancode::Space));
        REQUIRE_FALSE(harness.Call<bool>("InputNative.KeyJustReleased", Key::Space));
        REQUIRE_FALSE(harness.Call<bool>("InputNative.ScancodeJustReleased", Scancode::Space));
        REQUIRE(harness.Call<KeyModifiers>("InputNative.KeyModifiersOf") == KeyModifiers::None);
        REQUIRE_FALSE(harness.Call<bool>("InputNative.MouseIsDown", MouseButton::Left));
        REQUIRE_FALSE(harness.Call<bool>("InputNative.MouseJustPressed", MouseButton::Left));
        REQUIRE_FALSE(harness.Call<bool>("InputNative.MouseJustReleased", MouseButton::Left));

        // The outputs through the registration's zeroing (ADR-0045), as C# reaches them: each starts off nonzero and
        // comes back zero, because the registration zeroes it and the native then writes nothing.
        Math::Vec2 position(3.0f, 4.0f);
        harness.Call<void>("InputNative.MousePosition", &position);
        REQUIRE(position == Math::Vec2(0.0f, 0.0f));
        Math::Vec2 motion(3.0f, 4.0f);
        harness.Call<void>("InputNative.MouseMotion", &motion);
        REQUIRE(motion == Math::Vec2(0.0f, 0.0f));
        Math::Vec2 scroll(3.0f, 4.0f);
        harness.Call<void>("InputNative.MouseScroll", &scroll);
        REQUIRE(scroll == Math::Vec2(0.0f, 0.0f));

        GamepadId pads[2] = {GamepadId{7}, GamepadId{8}};
        REQUIRE(harness.Call<std::int32_t>("InputNative.GamepadsConnected", pads, 2) == 0);
        {
            INFO("A span was written to with no pads connected");
            REQUIRE(pads[0] == GamepadId{7});
        }
        REQUIRE_FALSE(harness.Call<bool>("InputNative.GamepadIsConnected", pad));
        REQUIRE_FALSE(harness.Call<bool>("InputNative.GamepadIsDown", pad, GamepadButton::South));
        REQUIRE_FALSE(harness.Call<bool>("InputNative.GamepadJustPressed", pad, GamepadButton::South));
        REQUIRE_FALSE(harness.Call<bool>("InputNative.GamepadJustReleased", pad, GamepadButton::South));
        REQUIRE(harness.Call<float>("InputNative.GamepadAxisOf", pad, GamepadAxis::LeftX) == 0.0f);
    }

    for (const char* native :
         {"InputNative.KeyIsDown", "InputNative.ScancodeIsDown", "InputNative.KeyJustPressed",
          "InputNative.ScancodeJustPressed", "InputNative.KeyJustReleased", "InputNative.ScancodeJustReleased",
          "InputNative.KeyModifiersOf", "InputNative.MouseIsDown", "InputNative.MouseJustPressed",
          "InputNative.MouseJustReleased", "InputNative.MousePosition", "InputNative.MouseMotion",
          "InputNative.MouseScroll", "InputNative.GamepadsConnected", "InputNative.GamepadIsConnected",
          "InputNative.GamepadIsDown", "InputNative.GamepadJustPressed", "InputNative.GamepadJustReleased",
          "InputNative.GamepadAxisOf"})
    {
        INFO(native << " did not warn once in a debug build (never in a release build), though called twice");
        REQUIRE(harness.WarnedOnce(native, InputScripting::NoInput));
    }
    {
        INFO("A native warned under a hand-typed facade name rather than its own");
        REQUIRE_FALSE(harness.Logger().Contains(Testing::LogLevel::Warn, "Keyboard."));
        REQUIRE_FALSE(harness.Logger().Contains(Testing::LogLevel::Warn, "Mouse."));
        REQUIRE_FALSE(harness.Logger().Contains(Testing::LogLevel::Warn, "Gamepads."));
    }
}

TEST_CASE("With no input installed, the InputClaims C# reads is not found", "[input][scripting][rules]")
{
    Testing::NativeCallHarness harness;
    const auto input = std::make_shared<InputScripting>();
    const ResourceRegistration claims = InputClaimsResource(input);
    {
        INFO("The InputClaims copy was found in an app with no InputClaims");
        REQUIRE(claims.find(harness.GetApp()) == nullptr);
    }
    {
        INFO("Not finding the claims warned");
        REQUIRE_FALSE(harness.Logger().Contains(Testing::LogLevel::Warn, "input scripting"));
    }
}
