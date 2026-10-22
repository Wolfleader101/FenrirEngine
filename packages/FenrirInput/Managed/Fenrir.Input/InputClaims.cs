using System.Runtime.CompilerServices;
using Fenrir.Math;
using Fenrir.Scripting;

namespace Fenrir.Input;

// The engine fills these fields in native memory; the compiler cannot see that.
#pragma warning disable CS0649

/// <summary>A rectangle in a window's logical coordinates, from its top-left corner (the mouse's units).</summary>
public readonly struct CaretRect
{
    public readonly Vector2 Position;
    public readonly Vector2 Size;
}

/// <summary>
/// What the input routers are doing with input in one window (ADR-0027): the union of every router's input claims there.
/// Claims describe the interaction; they never filter events.
/// </summary>
public readonly struct WindowInputClaims
{
    /// <summary>The pointer is over a router's UI.</summary>
    public readonly bool Pointer;
    /// <summary>A router has captured the pointer (a drag that began on its UI).</summary>
    public readonly bool PointerCapture;
    /// <summary>A router has the keyboard focus: the game's held keys in the window were cancelled.</summary>
    public readonly bool KeyboardFocus;
    /// <summary>A router wants text, with the caret at <see cref="Caret"/>.</summary>
    public readonly bool TextInput;
    /// <summary>Where text is being typed: read only with <see cref="TextInput"/>.</summary>
    public readonly CaretRect Caret;
}

/// <summary>One window's claims in <see cref="InputClaims"/>.</summary>
public readonly struct WindowClaimsEntry
{
    public readonly WindowId Window;
    public readonly WindowInputClaims Claims;
}

/// <summary>The fixed room <see cref="InputClaims"/> has for windows.</summary>
[InlineArray(InputClaims.MaxWindows)]
public struct WindowClaimsList
{
    private WindowClaimsEntry first;
}

/// <summary>
/// The union of the input claims every input router publishes (ADR-0027), FenrirInput's <c>InputClaims</c> app resource,
/// read through <c>Resources.GetReadOnly&lt;InputClaims&gt;()</c>: per window, each claim is raised if any router raises it,
/// and the caret is that of the highest-priority router claiming text input; the gamepad claim is app-wide. Routers stay
/// C++, so scripts read the claims and never publish them.
/// </summary>
/// <remarks>
/// The view is of the claims as they were when it was got: get it again in a later system to see newer ones. It holds up to
/// <see cref="MaxWindows"/> windows; claims in more windows than that are left out, with a warning in a debug build.
/// With no input installed in the app there is none (<c>Resources.TryGetReadOnly</c> returns false).
/// </remarks>
[NativeResource("Fenrir.Input.InputClaims")]
public readonly struct InputClaims : IReadOnlyResource
{
    /// <summary>How many windows the claims can hold.</summary>
    public const int MaxWindows = 8;

    private readonly WindowClaimsList windows;
    private readonly uint windowCount;
    private readonly bool gamepad;

    /// <summary>Whether any router claims the gamepads, for UI navigation.</summary>
    public bool Gamepad => gamepad;

    /// <summary>How many windows any router claims something in.</summary>
    public int WindowCount => (int)System.Math.Min(windowCount, (uint)MaxWindows);

    /// <summary>The window and claims at <paramref name="index"/>, from 0 to <see cref="WindowCount"/> less one.</summary>
    public WindowClaimsEntry this[int index]
    {
        get
        {
            if ((uint)index >= (uint)WindowCount)
                throw new ArgumentOutOfRangeException(nameof(index));
            return windows[index];
        }
    }

    /// <summary>The claims raised in <paramref name="window"/>; none raised for a window no router claims anything in.</summary>
    public WindowInputClaims For(WindowId window)
    {
        int count = WindowCount;
        for (int index = 0; index < count; ++index)
        {
            if (windows[index].Window == window)
                return windows[index].Claims;
        }
        return default;
    }
}
