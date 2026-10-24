using System.Runtime.CompilerServices;
using Fenrir.Input;
using Fenrir.Scripting;
using Fenrir.Window;

// Outside any Fenrir.* namespace, as a game's scripts are, so `Window` names the class and not the namespace.
namespace WindowScriptFixture;

// The window as C# sees it, called from the tests outside any system.
public static class WindowCalls
{
    public static uint Main() => Window.Main.Value;

    public static bool Exists(uint window) => Window.Exists(new WindowId(window));

    public static int Width(uint window) => Window.Size(new WindowId(window)).Width;

    public static int Height(uint window) => Window.Size(new WindowId(window)).Height;

    public static int PixelWidth(uint window) => Window.PixelSize(new WindowId(window)).Width;

    public static int PixelHeight(uint window) => Window.PixelSize(new WindowId(window)).Height;

    public static float ContentScale(uint window) => Window.ContentScale(new WindowId(window));

    public static bool IsMinimised(uint window) => Window.IsMinimised(new WindowId(window));

    public static bool IsFocused(uint window) => Window.IsFocused(new WindowId(window));

    public static int GetCursorMode(uint window) => (int)Window.GetCursorMode(new WindowId(window));

    public static bool SetCursorMode(uint window, int mode) => Window.SetCursorMode(new WindowId(window), (CursorMode)mode);

    public static bool SetCursorShape(uint window, int shape) =>
        Window.SetCursorShape(new WindowId(window), (CursorShape)shape);

    public static bool SetTitle(uint window, string title) => Window.SetTitle(new WindowId(window), title);

    public static bool SetClipboard(string text) => Window.SetClipboard(text);

    // What GetClipboard returns given a span of `capacity` chars.
    public static int ClipboardLength(int capacity)
    {
        Span<char> text = capacity <= 256 ? stackalloc char[capacity] : new char[capacity];
        return Window.GetClipboard(text);
    }

    // What GetClipboard wrote to a span of `capacity` chars.
    public static string Clipboard(int capacity)
    {
        Span<char> text = capacity <= 256 ? stackalloc char[capacity] : new char[capacity];
        int length = Window.GetClipboard(text);
        return new string(text[..System.Math.Min(length, capacity)]);
    }

    public static bool RequestClose(uint window) => Window.RequestClose(new WindowId(window));

    public static int WindowSizeSize() => Unsafe.SizeOf<WindowSize>();
}

// Reads the window's events every frame, as game code would, and keeps what it read for the tests.
[System(Phase.Update)]
public sealed class WindowEventRecorder : ISystem
{
    private readonly EventReader<WindowResizedEvent> resizes = new();
    private readonly EventReader<WindowCloseRequestedEvent> closes = new();
    private readonly EventReader<WindowFocusEvent> focus = new();

    public void OnUpdate(in SystemContext context)
    {
        foreach (var resized in resizes.Read())
            WindowEvents.Add($"Resized {resized.Window.Value} {resized.LogicalSize.Width}x{resized.LogicalSize.Height}");
        foreach (var close in closes.Read())
            WindowEvents.Add($"CloseRequested {close.Window.Value}");
        foreach (var change in focus.Read())
            WindowEvents.Add($"Focus {change.Window.Value} {(change.Focused ? 1 : 0)}");
    }
}

public static class WindowEvents
{
    private static readonly List<string> events = new();

    internal static void Add(string line) => events.Add(line);

    public static void Clear() => events.Clear();

    // Every event read since the last Clear, one per line.
    public static string Log() => string.Join("\n", events);
}
