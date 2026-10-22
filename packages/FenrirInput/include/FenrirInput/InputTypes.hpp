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
#define FENRIR_INPUT_KEYS(X) \
    X(A) X(B) X(C) X(D) X(E) X(F) X(G) X(H) X(I) X(J) X(K) X(L) X(M) \
    X(N) X(O) X(P) X(Q) X(R) X(S) X(T) X(U) X(V) X(W) X(X) X(Y) X(Z) \
    X(Num0) X(Num1) X(Num2) X(Num3) X(Num4) X(Num5) X(Num6) X(Num7) X(Num8) X(Num9) \
    X(Escape) X(Enter) X(Tab) X(Backspace) X(Space) \
    X(Minus) X(Equals) X(LeftBracket) X(RightBracket) X(Backslash) X(Semicolon) X(Apostrophe) X(Grave) \
    X(Comma) X(Period) X(Slash) X(CapsLock) \
    X(F1) X(F2) X(F3) X(F4) X(F5) X(F6) X(F7) X(F8) X(F9) X(F10) X(F11) X(F12) \
    X(F13) X(F14) X(F15) X(F16) X(F17) X(F18) X(F19) X(F20) X(F21) X(F22) X(F23) X(F24) \
    X(PrintScreen) X(ScrollLock) X(Pause) X(Insert) X(Delete) X(Home) X(End) X(PageUp) X(PageDown) \
    X(Left) X(Right) X(Up) X(Down) \
    X(NumLock) X(KeypadDivide) X(KeypadMultiply) X(KeypadMinus) X(KeypadPlus) X(KeypadEnter) X(KeypadPeriod) \
    X(Keypad0) X(Keypad1) X(Keypad2) X(Keypad3) X(Keypad4) X(Keypad5) X(Keypad6) X(Keypad7) X(Keypad8) X(Keypad9) \
    X(Menu) \
    X(LeftCtrl) X(LeftShift) X(LeftAlt) X(LeftSuper) X(RightCtrl) X(RightShift) X(RightAlt) X(RightSuper)
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
        FENRIR_INPUT_KEYS(FENRIR_INPUT_KEY_ENUMERATOR) Count
    };

    /**
     * @brief A scancode: where a key sits on the keyboard, whatever the layout, named as a US QWERTY keyboard prints
     * it. `Scancode::W` is the key above `Scancode::S` on every layout. Use it for keys placed by position, such as
     * WASD.
     */
    enum class Scancode : std::uint16_t
    {
        Unknown,
        FENRIR_INPUT_KEYS(FENRIR_INPUT_KEY_ENUMERATOR) Count
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
        Count
    };

    /** @brief How many gamepad axes there are. */
    inline constexpr std::size_t GamepadAxisCount = static_cast<std::size_t>(GamepadAxis::Count);
} // namespace Fenrir
