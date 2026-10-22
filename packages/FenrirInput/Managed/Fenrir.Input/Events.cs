using Fenrir.Math;
using Fenrir.Scripting;

namespace Fenrir.Input;

// The engine fills these fields in native memory; the compiler cannot see that.
#pragma warning disable CS0649

// The C# mirrors of FenrirInput's events (FenrirInput/InputEvents.hpp): one for every input that reaches the game, sent in
// PreUpdate in the order the input was fed. Read them through an EventReader<T> field; each reader sees each event once,
// and C++ and C# readers share one queue. Input an input router consumed never arrives. Each is checked against its native
// layout whenever a script assembly loads, so a drift between the two fails the load and names the field.

/// <summary>A key went down, repeated while held, or went up.</summary>
[NativeEvent("Fenrir.Input.KeyEvent")]
public readonly struct KeyEvent : IEvent
{
    public readonly WindowId Window;
    /// <summary>The key as the keyboard layout names it.</summary>
    public readonly Key Key;
    /// <summary>Where the key sits on the keyboard.</summary>
    public readonly Scancode Scancode;
    /// <summary>The modifiers held when the key went down or up.</summary>
    public readonly KeyModifiers Modifiers;
    /// <summary>True when the key went down (or repeated), false when it went up.</summary>
    public readonly bool Pressed;
    /// <summary>True for the repeats a held key sends; a repeat is not a new press.</summary>
    public readonly bool Repeat;
    /// <summary>
    /// True for a release sent by a cancellation: a UI took the keyboard focus, or the window lost the focus, while the key
    /// was held. The key may still be down; its physical release goes to no one.
    /// </summary>
    public readonly bool Cancelled;
}

/// <summary>One Unicode codepoint of typed text: text typed as several codepoints is one event per codepoint.</summary>
[NativeEvent("Fenrir.Input.TextEvent")]
public readonly struct TextEvent : IEvent
{
    public readonly WindowId Window;
    /// <summary>The codepoint; <c>char.ConvertFromUtf32</c> makes it a string.</summary>
    public readonly uint Codepoint;
}

/// <summary>A mouse button went down or up.</summary>
[NativeEvent("Fenrir.Input.MouseButtonEvent")]
public readonly struct MouseButtonEvent : IEvent
{
    public readonly WindowId Window;
    public readonly MouseButton Button;
    /// <summary>True when the button went down, false when it went up.</summary>
    public readonly bool Pressed;
    /// <summary>
    /// True for a release sent by a cancellation: a UI captured the pointer, or the window lost the focus, while the button
    /// was held. The button may still be down; its physical release goes to no one.
    /// </summary>
    public readonly bool Cancelled;
}

/// <summary>The mouse moved over a window.</summary>
[NativeEvent("Fenrir.Input.MouseMotionEvent")]
public readonly struct MouseMotionEvent : IEvent
{
    public readonly WindowId Window;
    /// <summary>Where the pointer is now, in the window's logical coordinates (from its top-left corner).</summary>
    public readonly Vector2 Position;
    /// <summary>How far it moved, in the same units: raw motion while the cursor is captured.</summary>
    public readonly Vector2 Motion;
}

/// <summary>The mouse wheel turned.</summary>
[NativeEvent("Fenrir.Input.MouseScrollEvent")]
public readonly struct MouseScrollEvent : IEvent
{
    public readonly WindowId Window;
    /// <summary>How far, in notches: y positive away from the user, x positive to the right.</summary>
    public readonly Vector2 Notches;
}

/// <summary>
/// A window gained or lost the input focus (keyboard input). FenrirWindow feeds it, so it is the window's focus event too
/// (<c>Fenrir.Window</c> has no other).
/// </summary>
[NativeEvent("Fenrir.Input.WindowFocusEvent")]
public readonly struct WindowFocusEvent : IEvent
{
    public readonly WindowId Window;
    /// <summary>True when the window gained the focus, false when it lost it.</summary>
    public readonly bool Focused;
}

/// <summary>A gamepad was plugged in or unplugged.</summary>
[NativeEvent("Fenrir.Input.GamepadConnectionEvent")]
public readonly struct GamepadConnectionEvent : IEvent
{
    public readonly GamepadId Gamepad;
    /// <summary>True when it connected, false when it disconnected.</summary>
    public readonly bool Connected;
}

/// <summary>A gamepad button went down or up.</summary>
[NativeEvent("Fenrir.Input.GamepadButtonEvent")]
public readonly struct GamepadButtonEvent : IEvent
{
    public readonly GamepadId Gamepad;
    public readonly GamepadButton Button;
    /// <summary>True when the button went down, false when it went up.</summary>
    public readonly bool Pressed;
}

/// <summary>A gamepad axis moved.</summary>
[NativeEvent("Fenrir.Input.GamepadAxisEvent")]
public readonly struct GamepadAxisEvent : IEvent
{
    public readonly GamepadId Gamepad;
    public readonly GamepadAxis Axis;
    /// <summary>Its new value: -1 to 1 for a stick, 0 to 1 for a trigger.</summary>
    public readonly float Value;
}
