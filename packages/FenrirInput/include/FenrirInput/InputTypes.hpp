#pragma once

#include <cstddef>
#include <cstdint>

// FenrirInput's device-neutral types (ADR-0017). Nothing here names a windowing library or the OS: FenrirWindow
// translates what its library delivers into these before passing it to the input feed.

namespace Fenrir
{
    /**
     * @brief Which window an input came from. A plain number chosen by whoever feeds the input (FenrirWindow, which
     * keys its windows by it, or a test); FenrirInput only compares it.
     */
    struct WindowId
    {
        std::uint32_t value = 0;

        friend constexpr bool operator==(WindowId a, WindowId b) noexcept = default;
    };

    /**
     * @brief Which gamepad an input came from: a number its feeder gives the pad when it connects and never gives
     * another pad, so a pad that is unplugged and plugged back in comes back under a new id.
     */
    struct GamepadId
    {
        std::uint32_t value = 0;

        friend constexpr bool operator==(GamepadId a, GamepadId b) noexcept = default;
    };

// The keys a Key and a Scancode can name, in one list so the two enums always name the same keys. Letters, digits
// and punctuation are named as a US QWERTY keyboard prints them.
// clang-format off
#define FENRIR_INPUT_KEYS(KEY) \
    KEY(A) KEY(B) KEY(C) KEY(D) KEY(E) KEY(F) KEY(G) KEY(H) KEY(I) KEY(J) KEY(K) KEY(L) KEY(M) KEY(N) KEY(O) KEY(P) \
    KEY(Q) KEY(R) KEY(S) KEY(T) KEY(U) KEY(V) KEY(W) KEY(X) KEY(Y) KEY(Z) KEY(Num0) KEY(Num1) KEY(Num2) KEY(Num3) \
    KEY(Num4) KEY(Num5) KEY(Num6) KEY(Num7) KEY(Num8) KEY(Num9) KEY(Escape) KEY(Enter) KEY(Tab) KEY(Backspace) \
    KEY(Space) KEY(Minus) KEY(Equals) KEY(LeftBracket) KEY(RightBracket) KEY(Backslash) KEY(Semicolon) KEY(Apostrophe) \
    KEY(Grave) KEY(Comma) KEY(Period) KEY(Slash) KEY(CapsLock) KEY(F1) KEY(F2) KEY(F3) KEY(F4) KEY(F5) KEY(F6) KEY(F7) \
    KEY(F8) KEY(F9) KEY(F10) KEY(F11) KEY(F12) KEY(F13) KEY(F14) KEY(F15) KEY(F16) KEY(F17) KEY(F18) KEY(F19) KEY(F20) \
    KEY(F21) KEY(F22) KEY(F23) KEY(F24) KEY(PrintScreen) KEY(ScrollLock) KEY(Pause) KEY(Insert) KEY(Delete) KEY(Home) \
    KEY(End) KEY(PageUp) KEY(PageDown) KEY(Left) KEY(Right) KEY(Up) KEY(Down) KEY(NumLock) KEY(KeypadDivide) \
    KEY(KeypadMultiply) KEY(KeypadMinus) KEY(KeypadPlus) KEY(KeypadEnter) KEY(KeypadPeriod) KEY(Keypad0) KEY(Keypad1) \
    KEY(Keypad2) KEY(Keypad3) KEY(Keypad4) KEY(Keypad5) KEY(Keypad6) KEY(Keypad7) KEY(Keypad8) KEY(Keypad9) KEY(Menu) \
    KEY(LeftCtrl) KEY(LeftShift) KEY(LeftAlt) KEY(LeftSuper) KEY(RightCtrl) KEY(RightShift) KEY(RightAlt) \
    KEY(RightSuper)
    // clang-format on

#define FENRIR_INPUT_KEY_ENUMERATOR(name) name,

    /**
     * @brief A key code: which key was pressed as the keyboard layout names it. The key that types Z on an AZERTY
     * keyboard is `Key::W` on QWERTY and `Key::Z` on AZERTY. Use it for shortcuts named by letter. A key the layout
     * names with a character outside this list is `Unknown`.
     */
    enum class Key : std::uint16_t
    {
        Unknown,
        FENRIR_INPUT_KEYS(FENRIR_INPUT_KEY_ENUMERATOR)
        /** @brief How many keys there are, `Unknown` included: a size, not a key. */
        Count
    };

    /**
     * @brief A scancode: where a key sits on the keyboard, whatever the layout, named as a US QWERTY keyboard prints
     * it. `Scancode::W` is the key above `Scancode::S` on every layout. Use it for keys placed by position, such as
     * WASD.
     */
    enum class Scancode : std::uint16_t
    {
        Unknown,
        FENRIR_INPUT_KEYS(FENRIR_INPUT_KEY_ENUMERATOR)
        /** @brief How many scancodes there are, `Unknown` included: a size, not a key. */
        Count
    };

#undef FENRIR_INPUT_KEY_ENUMERATOR
#undef FENRIR_INPUT_KEYS

    /** @brief How many key codes there are, `Unknown` included. */
    inline constexpr std::size_t KeyCount = static_cast<std::size_t>(Key::Count);
    /** @brief How many scancodes there are, `Unknown` included. */
    inline constexpr std::size_t ScancodeCount = static_cast<std::size_t>(Scancode::Count);

    /**
     * @brief The modifiers held, or toggled on (`CapsLock`, `NumLock`), when a key went down or up: a set of bits,
     * combined with `|` and tested with HasModifiers. Either Shift key sets `Shift`, and so on.
     */
    enum class KeyModifiers : std::uint16_t
    {
        None = 0,
        Shift = 1 << 0,
        Ctrl = 1 << 1,
        Alt = 1 << 2,
        Super = 1 << 3,
        CapsLock = 1 << 4,
        NumLock = 1 << 5,
    };

    /** @brief Both sets of modifiers. */
    [[nodiscard]] constexpr KeyModifiers operator|(KeyModifiers a, KeyModifiers b) noexcept
    {
        return static_cast<KeyModifiers>(static_cast<std::uint16_t>(a) | static_cast<std::uint16_t>(b));
    }

    /** @brief The modifiers in both sets. */
    [[nodiscard]] constexpr KeyModifiers operator&(KeyModifiers a, KeyModifiers b) noexcept
    {
        return static_cast<KeyModifiers>(static_cast<std::uint16_t>(a) & static_cast<std::uint16_t>(b));
    }

    /** @brief Adds the modifiers in `b` to `a`. */
    constexpr KeyModifiers& operator|=(KeyModifiers& a, KeyModifiers b) noexcept
    {
        a = a | b;
        return a;
    }

    /** @brief Whether `set` holds every modifier in `wanted`. */
    [[nodiscard]] constexpr bool HasModifiers(KeyModifiers set, KeyModifiers wanted) noexcept
    {
        return (set & wanted) == wanted;
    }

    /** @brief A mouse button. `X1` and `X2` are the side buttons, back and forward. */
    enum class MouseButton : std::uint8_t
    {
        Left,
        Right,
        Middle,
        X1,
        X2,
        /** @brief How many mouse buttons there are: a size, not a button. */
        Count
    };

    /** @brief How many mouse buttons there are. */
    inline constexpr std::size_t MouseButtonCount = static_cast<std::size_t>(MouseButton::Count);

    /**
     * @brief A gamepad button in the standard layout, named by position: `South` is A on an Xbox pad and Cross on a
     * PlayStation pad. `Back`, `Guide` and `Start` are the centre buttons; the paddles are those on the back of some
     * pads, and `Misc1` is an extra centre button (share, capture or mute).
     */
    enum class GamepadButton : std::uint8_t
    {
        South,
        East,
        West,
        North,
        Back,
        Guide,
        Start,
        LeftStick,
        RightStick,
        LeftShoulder,
        RightShoulder,
        DPadUp,
        DPadDown,
        DPadLeft,
        DPadRight,
        Misc1,
        RightPaddle1,
        LeftPaddle1,
        RightPaddle2,
        LeftPaddle2,
        Touchpad,
        /** @brief How many gamepad buttons there are: a size, not a button. */
        Count
    };

    /** @brief How many gamepad buttons there are. */
    inline constexpr std::size_t GamepadButtonCount = static_cast<std::size_t>(GamepadButton::Count);

    /**
     * @brief A gamepad axis in the standard layout. Stick axes run from -1 to 1, positive to the right and down;
     * triggers run from 0 (released) to 1 (fully pulled).
     */
    enum class GamepadAxis : std::uint8_t
    {
        LeftX,
        LeftY,
        RightX,
        RightY,
        LeftTrigger,
        RightTrigger,
        /** @brief How many gamepad axes there are: a size, not an axis. */
        Count
    };

    /** @brief How many gamepad axes there are. */
    inline constexpr std::size_t GamepadAxisCount = static_cast<std::size_t>(GamepadAxis::Count);
} // namespace Fenrir
