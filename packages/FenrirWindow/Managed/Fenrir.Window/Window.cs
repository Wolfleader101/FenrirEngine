using Fenrir.Input;
using Fenrir.Scripting;

namespace Fenrir.Window;

// The engine fills these fields in native memory; the compiler cannot see that.
#pragma warning disable CS0649

/// <summary>A width and a height: in logical units (a window's size) or in pixels (its backbuffer).</summary>
public struct WindowSize : IEquatable<WindowSize>
{
    public int Width;
    public int Height;

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

/// <summary>How the mouse cursor behaves over a window.</summary>
public enum CursorMode : byte
{
    /// <summary>Shown, and free to leave the window.</summary>
    Normal,
    /// <summary>Hidden over the window, and free to leave it.</summary>
    Hidden,
    /// <summary>Hidden and held in the window; the mouse reports raw relative motion (for mouse look).</summary>
    Captured,
}

/// <summary>Which system cursor is shown over a window. <see cref="Hidden"/> shows none.</summary>
public enum CursorShape : byte
{
    Arrow,
    /// <summary>The I-beam, over editable text.</summary>
    Text,
    ResizeEastWest,
    ResizeNorthSouth,
    ResizeNorthWestSouthEast,
    ResizeNorthEastSouthWest,
    /// <summary>The pointing hand, over a link or button.</summary>
    Hand,
    NotAllowed,
    Hidden,
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
/// logged once per process for each place in the engine's native code that noticed (not once per C# call site).
/// </para>
/// </remarks>
public static class Window
{
    /// <summary>The main window, whether or not it has been made yet.</summary>
    public static WindowId Main => new(1);

    /// <summary>Whether <paramref name="window"/> has been made.</summary>
    public static bool Exists(WindowId window) => WindowNative.Exists(window.Value);

    /// <summary><paramref name="window"/>'s size in logical units.</summary>
    public static WindowSize Size(WindowId window)
    {
        WindowNative.Size(window.Value, out WindowSize size);
        return size;
    }

    /// <summary><paramref name="window"/>'s size in pixels.</summary>
    public static WindowSize PixelSize(WindowId window)
    {
        WindowNative.PixelSize(window.Value, out WindowSize size);
        return size;
    }

    /// <summary><paramref name="window"/>'s pixels per logical unit.</summary>
    public static float ContentScale(WindowId window) => WindowNative.ContentScale(window.Value);

    /// <summary>Whether <paramref name="window"/> is minimised: nothing should be drawn to it.</summary>
    public static bool IsMinimised(WindowId window) => WindowNative.IsMinimised(window.Value);

    /// <summary>Whether <paramref name="window"/> has the input focus.</summary>
    public static bool IsFocused(WindowId window) => WindowNative.IsFocused(window.Value);

    /// <summary>How the cursor behaves over <paramref name="window"/>, as last set.</summary>
    public static CursorMode GetCursorMode(WindowId window) => (CursorMode)WindowNative.GetCursorMode(window.Value);

    /// <summary>Sets how the cursor behaves over <paramref name="window"/>.</summary>
    /// <returns>Whether it was set: false with no such window, or if the platform refuses.</returns>
    public static bool SetCursorMode(WindowId window, CursorMode mode) => WindowNative.SetCursorMode(window.Value, (byte)mode);

    /// <summary>Sets the cursor shown over <paramref name="window"/>.</summary>
    /// <returns>Whether it was set: false with no such window, or if the platform refuses.</returns>
    public static bool SetCursorShape(WindowId window, CursorShape shape) =>
        WindowNative.SetCursorShape(window.Value, (byte)shape);

    /// <summary>Sets <paramref name="window"/>'s title.</summary>
    /// <returns>Whether it was set: false with no such window, or if the platform refuses.</returns>
    public static bool SetTitle(WindowId window, ReadOnlySpan<char> title) => WindowNative.SetTitle(window.Value, title);

    /// <summary>
    /// Writes the system clipboard's text to <paramref name="text"/> and returns its whole length in chars: as much as fits
    /// is written, so a result larger than the span says how large a span would hold it. Zero when the clipboard holds no
    /// text.
    /// </summary>
    public static unsafe int GetClipboard(Span<char> text)
    {
        fixed (char* first = text)
            return WindowNative.GetClipboard(first, text.Length);
    }

    /// <summary>Puts <paramref name="text"/> on the system clipboard.</summary>
    /// <returns>Whether it was put there: false with no window, or if the platform refuses.</returns>
    public static bool SetClipboard(ReadOnlySpan<char> text) => WindowNative.SetClipboard(text);

    /// <summary>
    /// Asks for <paramref name="window"/> to close, as its close button does: a <see cref="WindowCloseRequestedEvent"/> is
    /// sent and, for the main window when the app stops on its close, the app stops. The window itself stays.
    /// </summary>
    /// <returns>Whether the request was made: false with no such window.</returns>
    public static bool RequestClose(WindowId window) => WindowNative.RequestClose(window.Value);
}

/// <summary>A window's logical size, pixel size or content scale changed. Carries the new values.</summary>
[NativeEvent("Fenrir.Window.WindowResizedEvent")]
public readonly struct WindowResizedEvent : IEvent
{
    public readonly WindowId Window;
    public readonly WindowSize LogicalSize;
    public readonly WindowSize PixelSize;
    /// <summary>Pixels per logical unit.</summary>
    public readonly float ContentScale;
}

/// <summary>
/// A window's close was requested: its close button, the app asked to quit, or <see cref="Fenrir.Window.Window.RequestClose"/>. When the
/// app stops on the main window's close, the main window's request has also stopped it; the window stays until teardown.
/// </summary>
[NativeEvent("Fenrir.Window.WindowCloseRequestedEvent")]
public readonly struct WindowCloseRequestedEvent : IEvent
{
    public readonly WindowId Window;
}

// The native functions FenrirWindowScripting gives C#, each named "Fenrir.Window.WindowNative.<Method>". Ids cross as
// their values and enums as their underlying integers; text goes in as UTF-8 and comes back as UTF-16.
internal static unsafe partial class WindowNative
{
    [NativeFunction]
    public static partial bool Exists(uint window);

    [NativeFunction]
    public static partial void Size(uint window, out WindowSize size);

    [NativeFunction]
    public static partial void PixelSize(uint window, out WindowSize size);

    [NativeFunction]
    public static partial float ContentScale(uint window);

    [NativeFunction]
    public static partial bool IsMinimised(uint window);

    [NativeFunction]
    public static partial bool IsFocused(uint window);

    [NativeFunction]
    public static partial byte GetCursorMode(uint window);

    [NativeFunction]
    public static partial bool SetCursorMode(uint window, byte mode);

    [NativeFunction]
    public static partial bool SetCursorShape(uint window, byte shape);

    [NativeFunction]
    public static partial bool SetTitle(uint window, ReadOnlySpan<char> title);

    // Writes at most `capacity` chars from `text` on, and returns the text's whole length.
    [NativeFunction]
    public static partial int GetClipboard(char* text, int capacity);

    [NativeFunction]
    public static partial bool SetClipboard(ReadOnlySpan<char> text);

    [NativeFunction]
    public static partial bool RequestClose(uint window);
}
