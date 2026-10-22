// Gamepads: hot plug, and buttons and axes per pad (ADR-0017).

#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "InputTestApp.hpp"

using Fenrir::GamepadAxis;
using Fenrir::GamepadButton;
using Fenrir::GamepadId;
using namespace InputTests;

namespace
{
    constexpr GamepadId first{7};
    constexpr GamepadId second{9};
} // namespace

TEST_CASE("Gamepads connect and disconnect while the app runs", "[input][gamepads]")
{
    InputTestApp test;
    INFO("no pad is connected at first");
    REQUIRE(test.Gamepads().Connected().empty());

    test.Feed().GamepadConnected(first);
    test.Feed().GamepadConnected(second);
    test.app.RunFrame(oneTick);
    INFO("both pads are connected, oldest first");
    REQUIRE(test.Gamepads().Connected() == std::vector<GamepadId>{first, second});
    REQUIRE(test.Gamepads().IsConnected(first));

    test.Feed().GamepadDisconnected(first);
    test.app.RunFrame(oneTick);
    INFO("the unplugged pad is gone and the other stays");
    REQUIRE_FALSE(test.Gamepads().IsConnected(first));
    REQUIRE(test.Gamepads().Connected() == std::vector<GamepadId>{second});
}

TEST_CASE("Each gamepad reads its own buttons and axes", "[input][gamepads]")
{
    InputTestApp test;
    test.Feed().GamepadConnected(first);
    test.Feed().GamepadConnected(second);
    test.Feed().GamepadButtonDown(first, GamepadButton::South);
    test.Feed().GamepadAxisMoved(second, GamepadAxis::LeftX, -0.5f);
    test.Feed().GamepadAxisMoved(second, GamepadAxis::RightTrigger, 1.0f);
    test.app.RunFrame(oneTick);

    const Fenrir::Gamepads& pads = test.Gamepads();
    INFO("the first pad's South button is held and just pressed");
    REQUIRE(pads.IsDown(first, GamepadButton::South));
    REQUIRE(pads.JustPressed(first, GamepadButton::South));
    INFO("the second pad's is not");
    REQUIRE_FALSE(pads.IsDown(second, GamepadButton::South));
    INFO("the second pad's axes have the values fed");
    REQUIRE_THAT(pads.Axis(second, GamepadAxis::LeftX), Catch::Matchers::WithinAbs(-0.5, 0.0));
    REQUIRE(pads.Axis(second, GamepadAxis::RightTrigger) == 1.0f);
    INFO("the first pad's axes are at rest");
    REQUIRE(pads.Axis(first, GamepadAxis::LeftX) == 0.0f);

    test.Feed().GamepadButtonUp(first, GamepadButton::South);
    test.app.RunFrame(oneTick);
    INFO("released, the button is just released and no longer held");
    REQUIRE(pads.JustReleased(first, GamepadButton::South));
    REQUIRE_FALSE(pads.IsDown(first, GamepadButton::South));
}

TEST_CASE("An unplugged gamepad reads nothing held", "[input][gamepads]")
{
    InputTestApp test;
    test.Feed().GamepadConnected(first);
    test.Feed().GamepadButtonDown(first, GamepadButton::East);
    test.Feed().GamepadAxisMoved(first, GamepadAxis::LeftY, 0.75f);
    test.app.RunFrame(oneTick);
    test.Feed().GamepadDisconnected(first);
    test.app.RunFrame(oneTick);

    INFO("its button is no longer held");
    REQUIRE_FALSE(test.Gamepads().IsDown(first, GamepadButton::East));
    INFO("and its axes read zero");
    REQUIRE(test.Gamepads().Axis(first, GamepadAxis::LeftY) == 0.0f);
}
