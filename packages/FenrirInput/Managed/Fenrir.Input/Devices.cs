using Fenrir.Math;
using Fenrir.Scripting;

namespace Fenrir.Input;

// The input devices from C#: facades of native functions over FenrirInput's Keyboard, Mouse and Gamepads app resources
// (ADR-0029: a resource with behaviour is a facade). Every answer comes from C++, so C# reads the same edges as C++ does.
//
// Input edges follow the reading phase (ADR-0017). Read from a Tick system, "just pressed" and "just released" are the
// edges since the last tick; read from any other phase (or outside a system), the edges since the last frame. A tap is
// never lost in a frame with no tick, nor seen twice in a frame with two. Mouse motion and scroll are summed the same way.
//
// Nothing here throws. With no input installed in the app, every function does nothing and returns false or zero; so does
// a gamepad function given a pad that is not connected. In a debug build of the engine a warning is logged once per process
// for each place in the engine's native code that noticed (not once per C# call site).

/// <summary>
/// The keyboard: keys held, just pressed and just released, by key code (<see cref="Key"/>) and by scancode
/// (<see cref="Scancode"/>), which are tracked separately; and the modifiers. FenrirInput's <c>Keyboard</c>.
/// </summary>
public static class Keyboard
{
    /// <summary>Whether the key named <paramref name="key"/> by the layout is held.</summary>
    public static bool IsDown(Key key) => InputNative.KeyIsDown((ushort)key);

    /// <summary>Whether the key at <paramref name="scancode"/> is held.</summary>
    public static bool IsDown(Scancode scancode) => InputNative.ScancodeIsDown((ushort)scancode);

    /// <summary>Whether <paramref name="key"/> went down since the last tick or frame. Repeats are not presses.</summary>
    public static bool JustPressed(Key key) => InputNative.KeyJustPressed((ushort)key);

    /// <summary>Whether the key at <paramref name="scancode"/> went down since the last tick or frame.</summary>
    public static bool JustPressed(Scancode scancode) => InputNative.ScancodeJustPressed((ushort)scancode);

    /// <summary>Whether <paramref name="key"/> went up since the last tick or frame.</summary>
    public static bool JustReleased(Key key) => InputNative.KeyJustReleased((ushort)key);

    /// <summary>Whether the key at <paramref name="scancode"/> went up since the last tick or frame.</summary>
    public static bool JustReleased(Scancode scancode) => InputNative.ScancodeJustReleased((ushort)scancode);

    /// <summary>The modifiers of the latest key that went down or up.</summary>
    public static KeyModifiers Modifiers => (KeyModifiers)InputNative.KeyModifiers();
}

/// <summary>
/// The mouse: buttons held, just pressed and just released; the pointer's position; its motion and the wheel's scroll.
/// FenrirInput's <c>Mouse</c>. Position and motion are in the focused window's logical coordinates (from its top-left
/// corner), motion raw while the cursor is captured; scroll is in notches, y positive away from the user.
/// </summary>
public static class Mouse
{
    /// <summary>Whether <paramref name="button"/> is held.</summary>
    public static bool IsDown(MouseButton button) => InputNative.MouseIsDown((byte)button);

    /// <summary>Whether <paramref name="button"/> went down since the last tick or frame.</summary>
    public static bool JustPressed(MouseButton button) => InputNative.MouseJustPressed((byte)button);

    /// <summary>Whether <paramref name="button"/> went up since the last tick or frame.</summary>
    public static bool JustReleased(MouseButton button) => InputNative.MouseJustReleased((byte)button);

    /// <summary>Where the pointer last was.</summary>
    public static Vector2 Position
    {
        get
        {
            InputNative.MousePosition(out Vector2 position);
            return position;
        }
    }

    /// <summary>How far the pointer moved since the last tick or frame.</summary>
    public static Vector2 Motion
    {
        get
        {
            InputNative.MouseMotion(out Vector2 motion);
            return motion;
        }
    }

    /// <summary>How far the wheel turned since the last tick or frame, in notches.</summary>
    public static Vector2 Scroll
    {
        get
        {
            InputNative.MouseScroll(out Vector2 scroll);
            return scroll;
        }
    }
}

/// <summary>
/// The gamepads connected now, in the standard layout: per pad, buttons held, just pressed and just released, and each
/// axis's value. FenrirInput's <c>Gamepads</c>. A pad comes and goes with its <see cref="GamepadConnectionEvent"/>.
/// </summary>
public static class Gamepads
{
    /// <summary>
    /// Writes the pads connected now, oldest connection first, to <paramref name="pads"/>, and returns how many are
    /// connected. When more are connected than the span holds, the oldest are written.
    /// </summary>
    public static unsafe int Connected(Span<GamepadId> pads)
    {
        fixed (GamepadId* first = pads)
            return InputNative.GamepadsConnected((uint*)first, pads.Length);
    }

    /// <summary>Whether <paramref name="gamepad"/> is connected.</summary>
    public static bool IsConnected(GamepadId gamepad) => InputNative.GamepadIsConnected(gamepad.Value);

    /// <summary>Whether <paramref name="button"/> is held on <paramref name="gamepad"/>.</summary>
    public static bool IsDown(GamepadId gamepad, GamepadButton button) =>
        InputNative.GamepadIsDown(gamepad.Value, (byte)button);

    /// <summary>Whether <paramref name="button"/> went down on <paramref name="gamepad"/> since the last tick or frame.</summary>
    public static bool JustPressed(GamepadId gamepad, GamepadButton button) =>
        InputNative.GamepadJustPressed(gamepad.Value, (byte)button);

    /// <summary>Whether <paramref name="button"/> went up on <paramref name="gamepad"/> since the last tick or frame.</summary>
    public static bool JustReleased(GamepadId gamepad, GamepadButton button) =>
        InputNative.GamepadJustReleased(gamepad.Value, (byte)button);

    /// <summary>The value of <paramref name="axis"/> on <paramref name="gamepad"/>: -1 to 1 for a stick, 0 to 1 for a trigger.</summary>
    public static float Axis(GamepadId gamepad, GamepadAxis axis) => InputNative.GamepadAxis(gamepad.Value, (byte)axis);
}

// The native functions FenrirInputScripting gives C#, each named "Fenrir.Input.InputNative.<Method>". Enums cross as their
// underlying integers and ids as their values.
internal static unsafe partial class InputNative
{
    [NativeFunction]
    public static partial bool KeyIsDown(ushort key);

    [NativeFunction]
    public static partial bool ScancodeIsDown(ushort scancode);

    [NativeFunction]
    public static partial bool KeyJustPressed(ushort key);

    [NativeFunction]
    public static partial bool ScancodeJustPressed(ushort scancode);

    [NativeFunction]
    public static partial bool KeyJustReleased(ushort key);

    [NativeFunction]
    public static partial bool ScancodeJustReleased(ushort scancode);

    [NativeFunction]
    public static partial ushort KeyModifiers();

    [NativeFunction]
    public static partial bool MouseIsDown(byte button);

    [NativeFunction]
    public static partial bool MouseJustPressed(byte button);

    [NativeFunction]
    public static partial bool MouseJustReleased(byte button);

    [NativeFunction]
    public static partial void MousePosition(out Vector2 position);

    [NativeFunction]
    public static partial void MouseMotion(out Vector2 motion);

    [NativeFunction]
    public static partial void MouseScroll(out Vector2 scroll);

    // Writes at most `capacity` pad ids from `pads` on, and returns how many pads are connected.
    [NativeFunction]
    public static partial int GamepadsConnected(uint* pads, int capacity);

    [NativeFunction]
    public static partial bool GamepadIsConnected(uint gamepad);

    [NativeFunction]
    public static partial bool GamepadIsDown(uint gamepad, byte button);

    [NativeFunction]
    public static partial bool GamepadJustPressed(uint gamepad, byte button);

    [NativeFunction]
    public static partial bool GamepadJustReleased(uint gamepad, byte button);

    [NativeFunction]
    public static partial float GamepadAxis(uint gamepad, byte axis);
}
