using Fenrir.Math;

namespace Fenrir.Input;

// The input devices from C#: facades of native functions over FenrirInput's Keyboard, Mouse and Gamepads app resources
// (ADR-0029: a resource with behaviour is a facade), over the generated InputNative (Generated/, from
// Scripting/src/InputScripting.cpp). Every answer comes from C++, so C# reads the same edges as C++ does.
//
// Input edges follow the reading phase (ADR-0017). Read from a Tick system, "just pressed" and "just released" are the
// edges since the last tick; read from any other phase (or outside a system), the edges since the last frame. A tap is
// never lost in a frame with no tick, nor seen twice in a frame with two. Mouse motion and scroll are summed the same way.
//
// Nothing here throws. With no input installed in the app, every function does nothing and returns false or zero; so does
// a gamepad function given a pad that is not connected. In a debug build of the engine a warning is logged once per scripting install
// for each place in the engine's native code that noticed (not once per C# call site).

/// <summary>
/// The keyboard: keys held, just pressed and just released, by key code (<see cref="Key"/>) and by scancode
/// (<see cref="Scancode"/>), which are tracked separately; and the modifiers. FenrirInput's <c>Keyboard</c>.
/// </summary>
public static class Keyboard
{
    /// <summary>Whether the key named <paramref name="key"/> by the layout is held.</summary>
    public static bool IsDown(Key key) => InputNative.KeyIsDown(key);

    /// <summary>Whether the key at <paramref name="scancode"/> is held.</summary>
    public static bool IsDown(Scancode scancode) => InputNative.ScancodeIsDown(scancode);

    /// <summary>Whether <paramref name="key"/> went down since the last tick or frame. Repeats are not presses.</summary>
    public static bool JustPressed(Key key) => InputNative.KeyJustPressed(key);

    /// <summary>Whether the key at <paramref name="scancode"/> went down since the last tick or frame.</summary>
    public static bool JustPressed(Scancode scancode) => InputNative.ScancodeJustPressed(scancode);

    /// <summary>Whether <paramref name="key"/> went up since the last tick or frame.</summary>
    public static bool JustReleased(Key key) => InputNative.KeyJustReleased(key);

    /// <summary>Whether the key at <paramref name="scancode"/> went up since the last tick or frame.</summary>
    public static bool JustReleased(Scancode scancode) => InputNative.ScancodeJustReleased(scancode);

    /// <summary>The modifiers of the latest key that went down or up.</summary>
    public static KeyModifiers Modifiers => InputNative.KeyModifiersOf();
}

/// <summary>
/// The mouse: buttons held, just pressed and just released; the pointer's position; its motion and the wheel's scroll.
/// FenrirInput's <c>Mouse</c>. Position and motion are in the focused window's logical coordinates (from its top-left
/// corner), motion raw while the cursor is captured; scroll is in notches, y positive away from the user.
/// </summary>
public static class Mouse
{
    /// <summary>Whether <paramref name="button"/> is held.</summary>
    public static bool IsDown(MouseButton button) => InputNative.MouseIsDown(button);

    /// <summary>Whether <paramref name="button"/> went down since the last tick or frame.</summary>
    public static bool JustPressed(MouseButton button) => InputNative.MouseJustPressed(button);

    /// <summary>Whether <paramref name="button"/> went up since the last tick or frame.</summary>
    public static bool JustReleased(MouseButton button) => InputNative.MouseJustReleased(button);

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
    public static int Connected(Span<GamepadId> pads) => InputNative.GamepadsConnected(pads);

    /// <summary>Whether <paramref name="gamepad"/> is connected.</summary>
    public static bool IsConnected(GamepadId gamepad) => InputNative.GamepadIsConnected(gamepad);

    /// <summary>Whether <paramref name="button"/> is held on <paramref name="gamepad"/>.</summary>
    public static bool IsDown(GamepadId gamepad, GamepadButton button) => InputNative.GamepadIsDown(gamepad, button);

    /// <summary>Whether <paramref name="button"/> went down on <paramref name="gamepad"/> since the last tick or frame.</summary>
    public static bool JustPressed(GamepadId gamepad, GamepadButton button) =>
        InputNative.GamepadJustPressed(gamepad, button);

    /// <summary>Whether <paramref name="button"/> went up on <paramref name="gamepad"/> since the last tick or frame.</summary>
    public static bool JustReleased(GamepadId gamepad, GamepadButton button) =>
        InputNative.GamepadJustReleased(gamepad, button);

    /// <summary>The value of <paramref name="axis"/> on <paramref name="gamepad"/>: -1 to 1 for a stick, 0 to 1 for a trigger.</summary>
    public static float Axis(GamepadId gamepad, GamepadAxis axis) => InputNative.GamepadAxisOf(gamepad, axis);
}
