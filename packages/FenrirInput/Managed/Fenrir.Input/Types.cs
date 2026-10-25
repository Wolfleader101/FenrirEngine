namespace Fenrir.Input;

// The behaviour of FenrirInput's ids. Their fields, and the enums (Key, Scancode, KeyModifiers, MouseButton, GamepadButton,
// GamepadAxis), are generated from FenrirInput/InputTypes.hpp (Generated/), so a value crosses unchanged. Each enum keeps
// C++'s Count enumerator: how many values come before it, a size and not an input. A WindowId names a window (the main
// window is 1, Fenrir.Window.Window.Main; 0 names none); a GamepadId names a pad until it disconnects, and a pad plugged
// back in comes back under a new id (learn them from Gamepads.Connected or GamepadConnectionEvent).

public partial struct WindowId : IEquatable<WindowId>
{
    public WindowId(uint value) => Value = value;

    public readonly bool Equals(WindowId other) => Value == other.Value;

    public override readonly bool Equals(object? obj) => obj is WindowId other && Equals(other);

    public override readonly int GetHashCode() => (int)Value;

    public static bool operator ==(WindowId a, WindowId b) => a.Value == b.Value;

    public static bool operator !=(WindowId a, WindowId b) => a.Value != b.Value;

    public override readonly string ToString() => $"WindowId({Value})";
}

public partial struct GamepadId : IEquatable<GamepadId>
{
    public GamepadId(uint value) => Value = value;

    public readonly bool Equals(GamepadId other) => Value == other.Value;

    public override readonly bool Equals(object? obj) => obj is GamepadId other && Equals(other);

    public override readonly int GetHashCode() => (int)Value;

    public static bool operator ==(GamepadId a, GamepadId b) => a.Value == b.Value;

    public static bool operator !=(GamepadId a, GamepadId b) => a.Value != b.Value;

    public override readonly string ToString() => $"GamepadId({Value})";
}
