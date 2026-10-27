using Fenrir.Math;

namespace Fenrir.Input;

// The input devices from C#: facades of native functions over FenrirInput's Keyboard, Mouse and Gamepads app resources
// (ADR-0029: a resource with behaviour is a facade), over the generated InputNative (Generated/, from
// Scripting/src/InputScripting.cpp). Every answer comes from C++, so C# reads the same edges as C++ does. Each facade's
// pass-throughs (the key, button, pad and axis queries) are generated beside it from the natives' @facade marks
// (Generated/Keyboard.cs, Generated/Mouse.cs, Generated/Gamepads.cs); this part holds what a description cannot say:
// the class docs and the properties over an output.
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
public static partial class Keyboard
{
    /// <summary>The modifiers of the latest key that went down or up.</summary>
    public static KeyModifiers Modifiers => InputNative.KeyModifiersOf();
}

/// <summary>
/// The mouse: buttons held, just pressed and just released; the pointer's position; its motion and the wheel's scroll.
/// FenrirInput's <c>Mouse</c>. Position and motion are in the focused window's logical coordinates (from its top-left
/// corner), motion raw while the cursor is captured; scroll is in notches, y positive away from the user.
/// </summary>
public static partial class Mouse
{
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
public static partial class Gamepads
{
}
