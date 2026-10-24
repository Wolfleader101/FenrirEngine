using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using static System.FormattableString;
using Fenrir.Input;
using Fenrir.Math;
using Fenrir.Scripting;

namespace Fenrir.InputScriptFixture;

// The mouse, the gamepads and the input claims as C# reads them, called from the tests outside any system (so the edges
// are the frame's, as for C++ read at the same moment).
public static class DeviceReads
{
    public static bool MouseIsDown(int button) => Mouse.IsDown((MouseButton)button);

    public static bool MouseJustPressed(int button) => Mouse.JustPressed((MouseButton)button);

    public static bool MouseJustReleased(int button) => Mouse.JustReleased((MouseButton)button);

    // 0 for x, 1 for y.
    public static float MousePosition(int axis) => Axis(Mouse.Position, axis);

    public static float MouseMotion(int axis) => Axis(Mouse.Motion, axis);

    public static float MouseScroll(int axis) => Axis(Mouse.Scroll, axis);

    public static int ConnectedCount() => Gamepads.Connected(Span<GamepadId>.Empty);

    // The connected pads' ids, oldest first, as "1,2".
    public static string ConnectedPads()
    {
        Span<GamepadId> pads = stackalloc GamepadId[8];
        int count = Gamepads.Connected(pads);
        return string.Join(",", pads[..System.Math.Min(count, pads.Length)].ToArray().Select(pad => pad.Value));
    }

    public static bool GamepadIsConnected(uint pad) => Gamepads.IsConnected(new GamepadId(pad));

    public static bool GamepadIsDown(uint pad, int button) => Gamepads.IsDown(new GamepadId(pad), (GamepadButton)button);

    public static bool GamepadJustPressed(uint pad, int button) =>
        Gamepads.JustPressed(new GamepadId(pad), (GamepadButton)button);

    public static bool GamepadJustReleased(uint pad, int button) =>
        Gamepads.JustReleased(new GamepadId(pad), (GamepadButton)button);

    public static float GamepadAxis(uint pad, int axis) => Gamepads.Axis(new GamepadId(pad), (GamepadAxis)axis);

    public static bool HasClaims() => Resources.TryGetReadOnly<InputClaims>(out _);

    public static bool GamepadClaimed() => Resources.GetReadOnly<InputClaims>().Value.Gamepad;

    public static int ClaimedWindowCount() => Resources.GetReadOnly<InputClaims>().Value.WindowCount;

    // One claim in `window`: 0 pointer, 1 pointer capture, 2 keyboard focus, 3 text input.
    public static bool Claim(uint window, int which)
    {
        WindowInputClaims claims = Resources.GetReadOnly<InputClaims>().Value.For(new WindowId(window));
        return which switch
        {
            0 => claims.Pointer,
            1 => claims.PointerCapture,
            2 => claims.KeyboardFocus,
            _ => claims.TextInput,
        };
    }

    // The caret in `window`: 0 and 1 its position's x and y, 2 and 3 its size's.
    public static float Caret(uint window, int which)
    {
        CaretRect caret = Resources.GetReadOnly<InputClaims>().Value.For(new WindowId(window)).Caret;
        return which < 2 ? Axis(caret.Position, which) : Axis(caret.Size, which - 2);
    }

    private static float Axis(Vector2 value, int axis) => axis == 0 ? value.X : value.Y;
}

// What C# reads of the input events other than keys, in an Update system, for the tests to compare with C++.
[System(Phase.Update)]
public sealed class InputEventRecorder : ISystem
{
    private readonly EventReader<TextEvent> text = new();
    private readonly EventReader<MouseButtonEvent> buttons = new();
    private readonly EventReader<MouseMotionEvent> motions = new();
    private readonly EventReader<MouseScrollEvent> scrolls = new();
    private readonly EventReader<WindowFocusEvent> focus = new();
    private readonly EventReader<GamepadConnectionEvent> connections = new();
    private readonly EventReader<GamepadButtonEvent> padButtons = new();
    private readonly EventReader<GamepadAxisEvent> axes = new();

    public void OnUpdate(in SystemContext context)
    {
        foreach (var e in text.Read())
            InputEvents.Add($"Text {e.Window.Value} {e.Codepoint}");
        foreach (var e in buttons.Read())
            InputEvents.Add($"MouseButton {e.Window.Value} {(int)e.Button} {Bit(e.Pressed)}{Bit(e.Cancelled)}");
        foreach (var e in motions.Read())
            InputEvents.Add(Invariant($"MouseMotion {e.Window.Value} {e.Position.X:F2} {e.Position.Y:F2} {e.Motion.X:F2} {e.Motion.Y:F2}"));
        foreach (var e in scrolls.Read())
            InputEvents.Add(Invariant($"MouseScroll {e.Window.Value} {e.Notches.X:F2} {e.Notches.Y:F2}"));
        foreach (var e in focus.Read())
            InputEvents.Add($"WindowFocus {e.Window.Value} {Bit(e.Focused)}");
        foreach (var e in connections.Read())
            InputEvents.Add($"GamepadConnection {e.Gamepad.Value} {Bit(e.Connected)}");
        foreach (var e in padButtons.Read())
            InputEvents.Add($"GamepadButton {e.Gamepad.Value} {(int)e.Button} {Bit(e.Pressed)}");
        foreach (var e in axes.Read())
            InputEvents.Add(Invariant($"GamepadAxis {e.Gamepad.Value} {(int)e.Axis} {e.Value:F2}"));
    }

    private static char Bit(bool value) => value ? '1' : '0';
}

public static class InputEvents
{
    private static readonly List<string> events = new();

    internal static void Add(string line) => events.Add(line);

    public static void Clear() => events.Clear();

    // Every event read so far, one per line, grouped by type in the order the recorder reads the types.
    public static string Log() => string.Join("\n", events);
}

// The C# mirrors' shapes, for the tests to check against C++.
public static class Shapes
{
    // Every member of the Fenrir.Input enum `name`, in value order, as "Name=value" lines.
    public static string EnumMembers(string name)
    {
        Type type = typeof(Key).Assembly.GetType("Fenrir.Input." + name)
                    ?? throw new ArgumentException($"Fenrir.Input has no enum {name}");
        var members = Enum.GetValues(type).Cast<object>()
                          .Select(value => (Name: Enum.GetName(type, value)!, Value: Convert.ToInt64(value)))
                          .OrderBy(member => member.Value);
        return string.Join("\n", members.Select(member => $"{member.Name}={member.Value}"));
    }

    // The size in bytes of the underlying type of the Fenrir.Input enum `name`.
    public static int EnumSize(string name) =>
        Marshal.SizeOf(Enum.GetUnderlyingType(typeof(Key).Assembly.GetType("Fenrir.Input." + name)!));

    public static int WindowIdSize() => Unsafe.SizeOf<WindowId>();

    public static int GamepadIdSize() => Unsafe.SizeOf<GamepadId>();

    // A WindowId's bytes read back as a number: its one field is the value.
    public static unsafe uint WindowIdBytes(uint value)
    {
        WindowId id = new(value);
        return *(uint*)&id;
    }
}
