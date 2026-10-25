using Fenrir.Input;

namespace Fenrir.Window;

// The behaviour of WindowSize. Its fields, the cursor enums and the events (WindowResizedEvent, WindowCloseRequestedEvent)
// are generated from FenrirWindow's headers (Generated/), and so is WindowNative, the native functions Window calls.
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
public static class Window
{
    /// <summary>The main window, whether or not it has been made yet.</summary>
    public static WindowId Main => new(1);

    /// <summary>Whether <paramref name="window"/> has been made.</summary>
    public static bool Exists(WindowId window) => WindowNative.Exists(window);

    /// <summary><paramref name="window"/>'s size in logical units.</summary>
    public static WindowSize Size(WindowId window)
    {
        WindowNative.Size(window, out WindowSize size);
        return size;
    }

    /// <summary><paramref name="window"/>'s size in pixels.</summary>
    public static WindowSize PixelSize(WindowId window)
    {
        WindowNative.PixelSize(window, out WindowSize size);
        return size;
    }

    /// <summary><paramref name="window"/>'s pixels per logical unit.</summary>
    public static float ContentScale(WindowId window) => WindowNative.ContentScale(window);

    /// <summary>Whether <paramref name="window"/> is minimised: nothing should be drawn to it.</summary>
    public static bool IsMinimised(WindowId window) => WindowNative.IsMinimised(window);

    /// <summary>Whether <paramref name="window"/> has the input focus.</summary>
    public static bool IsFocused(WindowId window) => WindowNative.IsFocused(window);

    /// <summary>How the cursor behaves over <paramref name="window"/>, as last set.</summary>
    public static CursorMode GetCursorMode(WindowId window) => WindowNative.GetCursorMode(window);

    /// <summary>Sets how the cursor behaves over <paramref name="window"/>.</summary>
    /// <returns>Whether it was set: false with no such window, or if the platform refuses.</returns>
    public static bool SetCursorMode(WindowId window, CursorMode mode) => WindowNative.SetCursorMode(window, mode);

    /// <summary>Sets the cursor shown over <paramref name="window"/>.</summary>
    /// <returns>Whether it was set: false with no such window, or if the platform refuses.</returns>
    public static bool SetCursorShape(WindowId window, CursorShape shape) => WindowNative.SetCursorShape(window, shape);

    /// <summary>Sets <paramref name="window"/>'s title.</summary>
    /// <returns>Whether it was set: false with no such window, or if the platform refuses.</returns>
    public static bool SetTitle(WindowId window, ReadOnlySpan<char> title) => WindowNative.SetTitle(window, title);

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
    public static bool RequestClose(WindowId window) => WindowNative.RequestClose(window);
}
