// The no-op rule (spec: "Scripting: the extensions"), with no .NET: with input's extension state but no input installed
// in the app, every input native function does nothing, returns false or zero, and warns once per native function under
// its name, through NativeCallHarness (the native guard spec: "Rule tests move onto it").

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "FenrirApp/App.hpp"
#include "FenrirInput/Devices.hpp"
#include "FenrirInput/InputTypes.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirScripting/ZeroingOutputs.hpp"
#include "InputClaimsResource.hpp"
#include "InputScriptingState.hpp"
#include "NativeCallHarness.hpp"

// The natives, as the generated registration declares them from its own file: no header the engine sees declares them.
namespace Fenrir::InputNatives
{
    bool KeyIsDown(const CallContext&, InputScripting&, Key key);
    bool ScancodeIsDown(const CallContext&, InputScripting&, Scancode scancode);
    bool KeyJustPressed(const CallContext&, InputScripting&, Key key);
    bool ScancodeJustPressed(const CallContext&, InputScripting&, Scancode scancode);
    bool KeyJustReleased(const CallContext&, InputScripting&, Key key);
    bool ScancodeJustReleased(const CallContext&, InputScripting&, Scancode scancode);
    KeyModifiers KeyModifiersOf(const CallContext&, InputScripting&);
    bool MouseIsDown(const CallContext&, InputScripting&, MouseButton button);
    bool MouseJustPressed(const CallContext&, InputScripting&, MouseButton button);
    bool MouseJustReleased(const CallContext&, InputScripting&, MouseButton button);
    void MousePosition(const CallContext&, InputScripting&, Math::Vec2* position);
    void MouseMotion(const CallContext&, InputScripting&, Math::Vec2* motion);
    void MouseScroll(const CallContext&, InputScripting&, Math::Vec2* scroll);
    std::int32_t GamepadsConnected(const CallContext&, InputScripting&, GamepadId* pads, std::int32_t capacity);
    bool GamepadIsConnected(const CallContext&, InputScripting&, GamepadId gamepad);
    bool GamepadIsDown(const CallContext&, InputScripting&, GamepadId gamepad, GamepadButton button);
    bool GamepadJustPressed(const CallContext&, InputScripting&, GamepadId gamepad, GamepadButton button);
    bool GamepadJustReleased(const CallContext&, InputScripting&, GamepadId gamepad, GamepadButton button);
    float GamepadAxisOf(const CallContext&, InputScripting&, GamepadId gamepad, GamepadAxis axis);
} // namespace Fenrir::InputNatives

namespace
{
    using namespace Fenrir;

    // A release build warns about nothing (WarnOnceRecord, ADR-0043); a debug build once per place and message.
#ifdef NDEBUG
    constexpr std::size_t once = 0;
#else
    constexpr std::size_t once = 1;
#endif

    constexpr GamepadId pad{1};
    constexpr const char* NoInput = "the app has no input (Input::Install), so it reads nothing";

    // How many warnings the app logged that read `text`.
    std::size_t Warnings(const Testing::NativeCallHarness& harness, const std::string& text)
    {
        const auto messages = harness.Logger().Messages();
        return static_cast<std::size_t>(
            std::count_if(messages.begin(), messages.end(), [&](const Testing::LoggedMessage& message) {
                return message.level == Testing::LogLevel::Warn && message.text.find(text) != std::string::npos;
            }));
    }

    // The warning the input extension's record logs for `message` in the native `native` (its type-and-method name).
    std::string Logged(const char* native, const std::string& message)
    {
        return "[input scripting] " + std::string(native) + ": " + message + " (warned once)";
    }
} // namespace

TEST_CASE("With no input installed, every input native reads false or zero and warns once under its name",
          "[input][scripting][rules]")
{
    Testing::NativeCallHarness harness;
    InputScripting input;

    // Each native twice, from one place, so a second call shows the warning was given once.
    for (int call = 0; call < 2; ++call)
    {
        INFO("Call " << call << ": a read did not give false or zero");
        REQUIRE_FALSE(InputNatives::KeyIsDown(harness.Context("InputNative.KeyIsDown"), input, Key::Space));
        REQUIRE_FALSE(
            InputNatives::ScancodeIsDown(harness.Context("InputNative.ScancodeIsDown"), input, Scancode::Space));
        REQUIRE_FALSE(InputNatives::KeyJustPressed(harness.Context("InputNative.KeyJustPressed"), input, Key::Space));
        REQUIRE_FALSE(InputNatives::ScancodeJustPressed(harness.Context("InputNative.ScancodeJustPressed"), input,
                                                        Scancode::Space));
        REQUIRE_FALSE(InputNatives::KeyJustReleased(harness.Context("InputNative.KeyJustReleased"), input, Key::Space));
        REQUIRE_FALSE(InputNatives::ScancodeJustReleased(harness.Context("InputNative.ScancodeJustReleased"), input,
                                                         Scancode::Space));
        REQUIRE(InputNatives::KeyModifiersOf(harness.Context("InputNative.KeyModifiersOf"), input) ==
                KeyModifiers::None);
        REQUIRE_FALSE(InputNatives::MouseIsDown(harness.Context("InputNative.MouseIsDown"), input, MouseButton::Left));
        REQUIRE_FALSE(
            InputNatives::MouseJustPressed(harness.Context("InputNative.MouseJustPressed"), input, MouseButton::Left));
        REQUIRE_FALSE(InputNatives::MouseJustReleased(harness.Context("InputNative.MouseJustReleased"), input,
                                                      MouseButton::Left));

        // The outputs through the registration's zeroing (ADR-0045), as C# reaches them: each starts off nonzero and
        // comes back zero, because the registration zeroes it and the native then writes nothing.
        Math::Vec2 position(3.0f, 4.0f);
        ZeroingOutputs<&InputNatives::MousePosition, 2>::Call(harness.Context("InputNative.MousePosition"), input,
                                                              &position);
        REQUIRE(position == Math::Vec2(0.0f, 0.0f));
        Math::Vec2 motion(3.0f, 4.0f);
        ZeroingOutputs<&InputNatives::MouseMotion, 2>::Call(harness.Context("InputNative.MouseMotion"), input, &motion);
        REQUIRE(motion == Math::Vec2(0.0f, 0.0f));
        Math::Vec2 scroll(3.0f, 4.0f);
        ZeroingOutputs<&InputNatives::MouseScroll, 2>::Call(harness.Context("InputNative.MouseScroll"), input, &scroll);
        REQUIRE(scroll == Math::Vec2(0.0f, 0.0f));

        GamepadId pads[2] = {GamepadId{7}, GamepadId{8}};
        REQUIRE(InputNatives::GamepadsConnected(harness.Context("InputNative.GamepadsConnected"), input, pads, 2) == 0);
        {
            INFO("A span was written to with no pads connected");
            REQUIRE(pads[0] == GamepadId{7});
        }
        REQUIRE_FALSE(InputNatives::GamepadIsConnected(harness.Context("InputNative.GamepadIsConnected"), input, pad));
        REQUIRE_FALSE(InputNatives::GamepadIsDown(harness.Context("InputNative.GamepadIsDown"), input, pad,
                                                  GamepadButton::South));
        REQUIRE_FALSE(InputNatives::GamepadJustPressed(harness.Context("InputNative.GamepadJustPressed"), input, pad,
                                                       GamepadButton::South));
        REQUIRE_FALSE(InputNatives::GamepadJustReleased(harness.Context("InputNative.GamepadJustReleased"), input, pad,
                                                        GamepadButton::South));
        REQUIRE(InputNatives::GamepadAxisOf(harness.Context("InputNative.GamepadAxisOf"), input, pad,
                                            GamepadAxis::LeftX) == 0.0f);
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
        REQUIRE(Warnings(harness, Logged(native, NoInput)) == once);
    }
    {
        INFO("A native warned under a hand-typed facade name rather than its own");
        REQUIRE(Warnings(harness, "Keyboard.") == 0);
        REQUIRE(Warnings(harness, "Mouse.") == 0);
        REQUIRE(Warnings(harness, "Gamepads.") == 0);
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
        REQUIRE(Warnings(harness, "input scripting") == 0);
    }
}
