#pragma once

// DemoGamepad: the first connected gamepad, as plain data, so a scene's own driving code (C++ or C#) can read the pad
// without knowing GLFW. DemoGamepads is the system that writes it. It is DemoCamera's pattern turned around: there C++
// reads what a scene wrote, here C++ writes what a scene reads.

#include <cstddef>
#include <cstdint>
#include <span>

#include <GLFW/glfw3.h>

#include "FenrirApp/App.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirMath/Math.hpp"

/**
 * @brief The first connected gamepad this frame, in GLFW's standard gamepad layout. All zero while none is
 * connected, so unplugging a pad releases everything it held.
 */
struct DemoGamepad
{
    std::int32_t Connected = 0;
    Fenrir::Math::Vec2 LeftStick = Fenrir::Math::Vec2(0.0f);  // Each axis [-1, 1]: +X right, +Y down, as GLFW's.
    Fenrir::Math::Vec2 RightStick = Fenrir::Math::Vec2(0.0f); // As LeftStick.
    float LeftTrigger = 0.0f;                                 // [0, 1]: 0 released.
    float RightTrigger = 0.0f;                                // As LeftTrigger.
    std::uint32_t Buttons = 0; // Bit n is held GLFW_GAMEPAD_BUTTON_n: A is bit 0, Back bit 6.

    /** @brief Whether `button` (a GLFW_GAMEPAD_BUTTON_*) is held. */
    [[nodiscard]] bool Held(int button) const
    {
        return (Buttons & (1u << static_cast<unsigned>(button))) != 0;
    }
};

/**
 * @brief Each frame, before the ticks: polls the first connected GLFW gamepad and writes it into every DemoGamepad in
 * the active scene. A scene that wants the pad puts one on an entity; the others never see it.
 */
class DemoGamepads
{
  public:
    void Update(Fenrir::App& app)
    {
        const DemoGamepad pad = Poll();
        app.GetActiveScene().GetEntityList().ForEach<DemoGamepad>([&pad](DemoGamepad& wanted) { wanted = pad; });
    }

  private:
    static DemoGamepad Poll()
    {
        DemoGamepad pad;
        for (int joystick = GLFW_JOYSTICK_1; joystick <= GLFW_JOYSTICK_LAST; ++joystick)
        {
            GLFWgamepadstate state;
            if (glfwGetGamepadState(joystick, &state) != GLFW_TRUE)
            {
                continue;
            }
            pad.Connected = 1;
            const std::span<const float> axes(state.axes);
            const std::span<const unsigned char> buttons(state.buttons);
            pad.LeftStick = Fenrir::Math::Vec2(axes[GLFW_GAMEPAD_AXIS_LEFT_X], axes[GLFW_GAMEPAD_AXIS_LEFT_Y]);
            pad.RightStick = Fenrir::Math::Vec2(axes[GLFW_GAMEPAD_AXIS_RIGHT_X], axes[GLFW_GAMEPAD_AXIS_RIGHT_Y]);
            // GLFW's triggers rest at -1.
            pad.LeftTrigger = 0.5f * (axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] + 1.0f);
            pad.RightTrigger = 0.5f * (axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] + 1.0f);
            for (std::size_t button = 0; button < buttons.size(); ++button)
            {
                if (buttons[button] == GLFW_PRESS)
                {
                    pad.Buttons |= 1u << button;
                }
            }
            break;
        }
        return pad;
    }
};
