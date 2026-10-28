using Fenrir.Input;

namespace Fenrir.Window;

// The behaviour of WindowSize. Its fields, the cursor enums and the events (WindowResizedEvent, WindowCloseRequestedEvent)
// are generated from FenrirWindow's headers (Generated/), and so are WindowNative, the native functions Window calls, and
// Window's pass-throughs (Generated/Window.cs, from the natives' @facade marks). This part of Window holds what a
// description cannot say: the main window's id.
public partial struct WindowSize : IEquatable<WindowSize>
{
    public WindowSize(int width, int height)
    {
        Width = width;
        Height = height;
    }

    public readonly bool Equals(WindowSize other) => Width == other.Width && Height == other.Height;

    public override readonly bool Equals(object? obj) => obj is WindowSize other && Equals(other);

    public override readonly int GetHashCode() => HashCode.Combine(Width, Height);

    public static bool operator ==(WindowSize a, WindowSize b) => a.Equals(b);

    public static bool operator !=(WindowSize a, WindowSize b) => !a.Equals(b);

    public override readonly string ToString() => $"{Width} x {Height}";
}

/// <summary>
/// The app's windows from C#, keyed by <see cref="WindowId"/>: their sizes, focus, cursor and title, the clipboard, and
/// close requests, as FenrirWindow's <c>Window</c> offers them in C++ (whose documentation says what each does). Only the
/// main window is made today.
/// </summary>
/// <remarks>
/// Inside a <c>Fenrir.*</c> namespace, <c>Window</c> names this namespace: write <c>Fenrir.Window.Window</c> there, or use
/// an alias. Sizes, scale, minimised and focus are as of this frame's PreUpdate. Focus changes arrive as
/// <c>Fenrir.Input.WindowFocusEvent</c>, resizes as <see cref="WindowResizedEvent"/> and close requests as
/// <see cref="WindowCloseRequestedEvent"/>.
/// <para>
/// Nothing here throws. With no window in the app (no FenrirWindow, or before the window is made) or a <see cref="WindowId"/>
/// that names no window, a function does nothing and returns <c>false</c>, zero, an empty size or text, or
/// <see cref="CursorMode.Normal"/>; the clipboard functions need the main window. In a debug build of the engine a warning is
/// logged once per scripting install for each place in the engine's native code that noticed (not once per C# call site).
/// </para>
/// </remarks>
public static partial class Window
{
    /// <summary>The main window, whether or not it has been made yet.</summary>
    public static WindowId Main => new(1);
}
