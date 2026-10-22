namespace Fenrir.Input;

// The C# mirrors of FenrirInput's device-neutral types (FenrirInput/InputTypes.hpp, ADR-0017). Each enum has the C++ enum's
// underlying type and values, so a value crosses unchanged; the input scripting tests check every name and value against
// C++. The C++ enums' Count enumerators are left out: they are sizes, not inputs.

/// <summary>
/// Which window an input came from, or which window a <c>Fenrir.Window</c> function acts on: FenrirInput's <c>WindowId</c>,
/// a plain number. The main window is 1 (<c>Fenrir.Window.Window.Main</c>); 0 names none.
/// </summary>
public struct WindowId : IEquatable<WindowId>
{
    public uint Value;

    public WindowId(uint value) => Value = value;

    public readonly bool Equals(WindowId other) => Value == other.Value;

    public override readonly bool Equals(object? obj) => obj is WindowId other && Equals(other);

    public override readonly int GetHashCode() => (int)Value;

    public static bool operator ==(WindowId a, WindowId b) => a.Value == b.Value;

    public static bool operator !=(WindowId a, WindowId b) => a.Value != b.Value;

    public override readonly string ToString() => $"WindowId({Value})";
}

/// <summary>
/// Which gamepad an input came from: a number given to a pad when it connects and never given to another pad, so a pad
/// unplugged and plugged back in comes back under a new id. Learn the ids from <see cref="Gamepads.Connected"/> or
/// <see cref="GamepadConnectionEvent"/>.
/// </summary>
public struct GamepadId : IEquatable<GamepadId>
{
    public uint Value;

    public GamepadId(uint value) => Value = value;

    public readonly bool Equals(GamepadId other) => Value == other.Value;

    public override readonly bool Equals(object? obj) => obj is GamepadId other && Equals(other);

    public override readonly int GetHashCode() => (int)Value;

    public static bool operator ==(GamepadId a, GamepadId b) => a.Value == b.Value;

    public static bool operator !=(GamepadId a, GamepadId b) => a.Value != b.Value;

    public override readonly string ToString() => $"GamepadId({Value})";
}

/// <summary>
/// A key code: which key was pressed as the keyboard layout names it. The key that types Z on an AZERTY keyboard is
/// <see cref="W"/> on QWERTY and <see cref="Z"/> on AZERTY. Use it for shortcuts named by letter. A key the layout names
/// with a character outside this list is <see cref="Unknown"/>.
/// </summary>
public enum Key : ushort
{
    Unknown,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Escape, Enter, Tab, Backspace, Space, Minus, Equals, LeftBracket, RightBracket, Backslash, Semicolon, Apostrophe, Grave,
    Comma, Period, Slash, CapsLock,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,
    PrintScreen, ScrollLock, Pause, Insert, Delete, Home, End, PageUp, PageDown, Left, Right, Up, Down,
    NumLock, KeypadDivide, KeypadMultiply, KeypadMinus, KeypadPlus, KeypadEnter, KeypadPeriod,
    Keypad0, Keypad1, Keypad2, Keypad3, Keypad4, Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,
    Menu, LeftCtrl, LeftShift, LeftAlt, LeftSuper, RightCtrl, RightShift, RightAlt, RightSuper,
}

/// <summary>
/// A scancode: where a key sits on the keyboard, whatever the layout, named as a US QWERTY keyboard prints it.
/// <see cref="W"/> is the key above <see cref="S"/> on every layout. Use it for keys placed by position, such as WASD.
/// </summary>
public enum Scancode : ushort
{
    Unknown,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Escape, Enter, Tab, Backspace, Space, Minus, Equals, LeftBracket, RightBracket, Backslash, Semicolon, Apostrophe, Grave,
    Comma, Period, Slash, CapsLock,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,
    PrintScreen, ScrollLock, Pause, Insert, Delete, Home, End, PageUp, PageDown, Left, Right, Up, Down,
    NumLock, KeypadDivide, KeypadMultiply, KeypadMinus, KeypadPlus, KeypadEnter, KeypadPeriod,
    Keypad0, Keypad1, Keypad2, Keypad3, Keypad4, Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,
    Menu, LeftCtrl, LeftShift, LeftAlt, LeftSuper, RightCtrl, RightShift, RightAlt, RightSuper,
}

/// <summary>
/// The modifiers held, or toggled on (<see cref="CapsLock"/>, <see cref="NumLock"/>), when a key went down or up. Either
/// Shift key sets <see cref="Shift"/>, and so on. Test with <c>HasFlag</c> or <c>&amp;</c>.
/// </summary>
[Flags]
public enum KeyModifiers : ushort
{
    None = 0,
    Shift = 1 << 0,
    Ctrl = 1 << 1,
    Alt = 1 << 2,
    Super = 1 << 3,
    CapsLock = 1 << 4,
    NumLock = 1 << 5,
}

/// <summary>A mouse button. <see cref="X1"/> and <see cref="X2"/> are the side buttons, back and forward.</summary>
public enum MouseButton : byte
{
    Left,
    Right,
    Middle,
    X1,
    X2,
}

/// <summary>
/// A gamepad button in the standard layout, named by position: <see cref="South"/> is A on an Xbox pad and Cross on a
/// PlayStation pad. <see cref="Back"/>, <see cref="Guide"/> and <see cref="Start"/> are the centre buttons; the paddles are
/// those on the back of some pads, and <see cref="Misc1"/> is an extra centre button (share, capture or mute).
/// </summary>
public enum GamepadButton : byte
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
}

/// <summary>
/// A gamepad axis in the standard layout. Stick axes run from -1 to 1, positive to the right and down; triggers run from 0
/// (released) to 1 (fully pulled).
/// </summary>
public enum GamepadAxis : byte
{
    LeftX,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,
}
