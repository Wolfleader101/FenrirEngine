#pragma once

#include "FenrirInput/InputTypes.hpp"
#include "FenrirMath/Math.hpp"

// FenrirInput's engine events (ADR-0017): one for every input fed through the input feed, sent in PreUpdate in the
// order the input was fed, and read through EventReader<T> (ADR-0012). Each is plain data.

namespace Fenrir
{
    /** @brief A key went down, repeated while held, or went up. */
    struct KeyEvent
    {
        WindowId window;
        /** @brief The key as the keyboard layout names it. */
        Key key = Key::Unknown;
        /** @brief Where the key sits on the keyboard. */
        Scancode scancode = Scancode::Unknown;
        /** @brief The modifiers held when the key went down or up. */
        KeyModifiers modifiers = KeyModifiers::None;
        /** @brief True when the key went down (or repeated), false when it went up. */
        bool pressed = false;
        /** @brief True for the repeats a held key sends; a repeat is not a new press. */
        bool repeat = false;
        /**
         * @brief True for a release sent by a cancellation (ADR-0027): a router raised keyboardFocus or the window lost
         * the focus while the key was held. The key may still be down; its physical release goes to no one.
         */
        bool cancelled = false;
    };

    /** @brief One Unicode codepoint of typed text: text fed as several codepoints is one event per codepoint. */
    struct TextEvent
    {
        WindowId window;
        char32_t codepoint = 0;
    };

    /** @brief A mouse button went down or up. */
    struct MouseButtonEvent
    {
        WindowId window;
        MouseButton button = MouseButton::Left;
        /** @brief True when the button went down, false when it went up. */
        bool pressed = false;
        /**
         * @brief True for a release sent by a cancellation (ADR-0027): a router raised pointerCapture or the window
         * lost the focus while the button was held. The button may still be down; its physical release goes to no one.
         */
        bool cancelled = false;
    };

    /** @brief The mouse moved over a window. */
    struct MouseMotionEvent
    {
        WindowId window;
        /** @brief Where the pointer is now, in the window's logical coordinates (from its top-left corner). */
        Math::Vec2 position{0.0f, 0.0f};
        /** @brief How far it moved, in the same units: raw motion while the cursor is captured. */
        Math::Vec2 motion{0.0f, 0.0f};
    };

    /** @brief The mouse wheel turned. */
    struct MouseScrollEvent
    {
        WindowId window;
        /** @brief How far, in notches: y positive away from the user, x positive to the right. */
        Math::Vec2 notches{0.0f, 0.0f};
    };

    /** @brief A window gained or lost the input focus (keyboard input). */
    struct WindowFocusEvent
    {
        WindowId window;
        /** @brief True when the window gained the focus, false when it lost it. */
        bool focused = false;
    };

    /** @brief A gamepad was plugged in or unplugged. */
    struct GamepadConnectionEvent
    {
        GamepadId gamepad;
        /** @brief True when it connected, false when it disconnected. */
        bool connected = false;
    };

    /** @brief A gamepad button went down or up. */
    struct GamepadButtonEvent
    {
        GamepadId gamepad;
        GamepadButton button = GamepadButton::South;
        /** @brief True when the button went down, false when it went up. */
        bool pressed = false;
    };

    /** @brief A gamepad axis moved. */
    struct GamepadAxisEvent
    {
        GamepadId gamepad;
        GamepadAxis axis = GamepadAxis::LeftX;
        /** @brief Its new value: -1 to 1 for a stick, 0 to 1 for a trigger. */
        float value = 0.0f;
    };
} // namespace Fenrir
