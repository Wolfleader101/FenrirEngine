// The mouse, the gamepads and the input claims from C#, read as C++ reads them, and the input events other than keys
// read through C# EventReaders (spec seam 7).

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "InputScriptingApp.hpp"

using namespace Fenrir;
using namespace InputScriptingTests;

namespace
{
    constexpr GamepadId pad{7};
} // namespace

TEST_CASE("The mouse and the gamepads read from C# as from C++, and their events arrive in C#", "[input][scripting]")
{
    InputScriptingApp test;
    test.Get().RunFrame(oneTick);
    test.Call<void>(EventsType, "Clear");

    test.Feed().FocusGained(mainWindow);
    test.Feed().MouseButtonDown(mainWindow, MouseButton::Right);
    test.Feed().MouseMove(mainWindow, Math::Vec2(10.5f, 20.0f), Math::Vec2(1.5f, -2.0f));
    test.Feed().MouseMove(mainWindow, Math::Vec2(12.0f, 19.0f), Math::Vec2(1.5f, -1.0f));
    test.Feed().Scroll(mainWindow, Math::Vec2(0.0f, 2.0f));
    test.Feed().Text(mainWindow, U"é");
    test.Feed().GamepadConnected(pad);
    test.Feed().GamepadButtonDown(pad, GamepadButton::South);
    test.Feed().GamepadAxisMoved(pad, GamepadAxis::LeftTrigger, 0.75f);
    test.Get().RunFrame(oneTick);

    const Mouse& mouse = test.Get().GetResource<Mouse>();
    const Gamepads& gamepads = test.Get().GetResource<Gamepads>();
    const int right = static_cast<int>(MouseButton::Right);
    {
        INFO("C# does not read the right button as C++ does: held and just pressed");
        REQUIRE(mouse.IsDown(MouseButton::Right));
        REQUIRE(test.Call<bool>(DevicesType, "MouseIsDown", right));
        REQUIRE(mouse.JustPressed(MouseButton::Right));
        REQUIRE(test.Call<bool>(DevicesType, "MouseJustPressed", right));
        REQUIRE_FALSE(test.Call<bool>(DevicesType, "MouseJustReleased", right));
        REQUIRE_FALSE(test.Call<bool>(DevicesType, "MouseIsDown", static_cast<int>(MouseButton::Left)));
    }
    {
        INFO("C# does not read the pointer where C++ does: at (12, 19), moved (3, -3) this frame, scrolled 2 notches");
        REQUIRE(mouse.Position() == Math::Vec2(12.0f, 19.0f));
        REQUIRE(test.Call<float>(DevicesType, "MousePosition", 0) == 12.0f);
        REQUIRE(test.Call<float>(DevicesType, "MousePosition", 1) == 19.0f);
        REQUIRE(mouse.Motion() == Math::Vec2(3.0f, -3.0f));
        REQUIRE(test.Call<float>(DevicesType, "MouseMotion", 0) == 3.0f);
        REQUIRE(-test.Call<float>(DevicesType, "MouseMotion", 1) == 3.0f);
        REQUIRE(mouse.Scroll() == Math::Vec2(0.0f, 2.0f));
        REQUIRE(test.Call<float>(DevicesType, "MouseScroll", 0) == 0.0f);
        REQUIRE(test.Call<float>(DevicesType, "MouseScroll", 1) == 2.0f);
    }
    const int south = static_cast<int>(GamepadButton::South);
    const int trigger = static_cast<int>(GamepadAxis::LeftTrigger);
    {
        INFO("C# does not see pad 7 connected, alone, as C++ does");
        REQUIRE(gamepads.IsConnected(pad));
        REQUIRE(test.Call<bool>(DevicesType, "GamepadIsConnected", pad.value));
        REQUIRE(test.Call<int>(DevicesType, "ConnectedCount") == 1);
        REQUIRE(test.Call<std::optional<std::string>>(DevicesType, "ConnectedPads").value_or("") == "7");
    }
    {
        INFO("C# does not read pad 7's South button held and just pressed, and its left trigger at 0.75, as C++ does");
        REQUIRE(gamepads.IsDown(pad, GamepadButton::South));
        REQUIRE(test.Call<bool>(DevicesType, "GamepadIsDown", pad.value, south));
        REQUIRE(test.Call<bool>(DevicesType, "GamepadJustPressed", pad.value, south));
        REQUIRE_FALSE(test.Call<bool>(DevicesType, "GamepadJustReleased", pad.value, south));
        REQUIRE(gamepads.Axis(pad, GamepadAxis::LeftTrigger) == 0.75f);
        REQUIRE(test.Call<float>(DevicesType, "GamepadAxis", pad.value, trigger) == 0.75f);
    }

    test.Feed().MouseButtonUp(mainWindow, MouseButton::Right);
    test.Feed().GamepadButtonUp(pad, GamepadButton::South);
    test.Get().RunFrame(oneTick);
    {
        INFO("The next frame, C# does not read both releases as C++ does, with nothing moved");
        REQUIRE(mouse.JustReleased(MouseButton::Right));
        REQUIRE(test.Call<bool>(DevicesType, "MouseJustReleased", right));
        REQUIRE_FALSE(test.Call<bool>(DevicesType, "MouseIsDown", right));
        REQUIRE(gamepads.JustReleased(pad, GamepadButton::South));
        REQUIRE(test.Call<bool>(DevicesType, "GamepadJustReleased", pad.value, south));
        REQUIRE_FALSE(test.Call<bool>(DevicesType, "GamepadIsDown", pad.value, south));
        REQUIRE(test.Call<float>(DevicesType, "MouseMotion", 0) == 0.0f);
    }

    test.Feed().GamepadDisconnected(pad);
    test.Get().RunFrame(oneTick);
    {
        INFO("Once pad 7 is unplugged C# does not read it as C++ does: not connected, its trigger zero");
        REQUIRE_FALSE(gamepads.IsConnected(pad));
        REQUIRE_FALSE(test.Call<bool>(DevicesType, "GamepadIsConnected", pad.value));
        REQUIRE(test.Call<int>(DevicesType, "ConnectedCount") == 0);
        REQUIRE(test.Call<float>(DevicesType, "GamepadAxis", pad.value, trigger) == 0.0f);
    }

    // Each frame the recorder reads each type in turn, so a frame's events are grouped by type, each in the order fed.
    const std::string expected = "Text 1 233\n"
                                 "MouseButton 1 1 10\n"
                                 "MouseMotion 1 10.50 20.00 1.50 -2.00\n"
                                 "MouseMotion 1 12.00 19.00 1.50 -1.00\n"
                                 "MouseScroll 1 0.00 2.00\n"
                                 "WindowFocus 1 1\n"
                                 "GamepadConnection 7 1\n"
                                 "GamepadButton 7 0 1\n"
                                 "GamepadAxis 7 4 0.75\n"
                                 "MouseButton 1 1 00\n"
                                 "GamepadButton 7 0 0\n"
                                 "GamepadConnection 7 0";
    const auto events = test.Call<std::optional<std::string>>(EventsType, "Log").value_or("");
    INFO("C# EventReaders did not read each event once; they read:\n" << events);
    REQUIRE(events == expected);
}

namespace
{
    // An input router that consumes nothing: it only publishes claims.
    class ClaimingRouter : public InputRouter
    {
      public:
        RouteResult Route(App&, const RoutedInput&) override
        {
            return RouteResult::Passed;
        }
    };
} // namespace

TEST_CASE("C# reads the input claims as the InputClaims resource holds them", "[input][scripting]")
{
    InputScriptingApp test;
    const InputRouterId router = Input::AddInputRouter(test.Get(), 10, std::make_shared<ClaimingRouter>());
    test.Get().RunFrame(oneTick);
    {
        INFO("With no claims, C# does not find the resource empty: no window in it and no gamepad claim");
        REQUIRE(test.Call<bool>(DevicesType, "HasClaims"));
        REQUIRE(test.Call<int>(DevicesType, "ClaimedWindowCount") == 0);
        REQUIRE_FALSE(test.Call<bool>(DevicesType, "GamepadClaimed"));
    }

    WindowInputClaims typing;
    typing.pointer = true;
    typing.textInput = true;
    typing.caret = CaretRect{Math::Vec2(3.0f, 4.0f), Math::Vec2(1.0f, 16.0f)};
    WindowInputClaims focused;
    focused.keyboardFocus = true;
    constexpr WindowId otherWindow{2};
    Input::SetInputClaims(test.Get(), router, mainWindow, typing);
    Input::SetInputClaims(test.Get(), router, otherWindow, focused);
    Input::SetGamepadClaim(test.Get(), router, true);

    const InputClaims& claims = test.Get().GetResource<InputClaims>();
    {
        INFO("C# does not read both windows' claims and the gamepad claim, as C++ does");
        REQUIRE(test.Call<int>(DevicesType, "ClaimedWindowCount") == 2);
        REQUIRE(claims.Gamepad());
        REQUIRE(test.Call<bool>(DevicesType, "GamepadClaimed"));
    }
    for (const WindowId window : {mainWindow, otherWindow})
    {
        const WindowInputClaims cpp = claims.For(window);
        INFO("C# does not read window " << window.value << "'s claims as C++ does");
        REQUIRE(test.Call<bool>(DevicesType, "Claim", window.value, 0) == cpp.pointer);
        REQUIRE(test.Call<bool>(DevicesType, "Claim", window.value, 1) == cpp.pointerCapture);
        REQUIRE(test.Call<bool>(DevicesType, "Claim", window.value, 2) == cpp.keyboardFocus);
        REQUIRE(test.Call<bool>(DevicesType, "Claim", window.value, 3) == cpp.textInput);
    }
    {
        INFO("C# does not read the main window's caret at (3, 4), 1 by 16");
        REQUIRE(test.Call<float>(DevicesType, "Caret", mainWindow.value, 0) == 3.0f);
        REQUIRE(test.Call<float>(DevicesType, "Caret", mainWindow.value, 1) == 4.0f);
        REQUIRE(test.Call<float>(DevicesType, "Caret", mainWindow.value, 2) == 1.0f);
        REQUIRE(test.Call<float>(DevicesType, "Caret", mainWindow.value, 3) == 16.0f);
    }
    {
        INFO("C# does not read pointer and text input raised in the main window, and keyboard focus only in the other");
        REQUIRE(test.Call<bool>(DevicesType, "Claim", mainWindow.value, 0));
        REQUIRE(test.Call<bool>(DevicesType, "Claim", mainWindow.value, 3));
        REQUIRE_FALSE(test.Call<bool>(DevicesType, "Claim", mainWindow.value, 2));
        REQUIRE(test.Call<bool>(DevicesType, "Claim", otherWindow.value, 2));
    }

    Input::SetInputClaims(test.Get(), router, otherWindow, WindowInputClaims{});
    INFO("A claim dropped in C++ is still there the next time C# gets the resource");
    REQUIRE_FALSE(test.Call<bool>(DevicesType, "Claim", otherWindow.value, 2));
}
