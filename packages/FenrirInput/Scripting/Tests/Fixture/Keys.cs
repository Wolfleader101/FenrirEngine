using Fenrir.Input;
using Fenrir.Scripting;

namespace Fenrir.InputScriptFixture;

// What the Tick and Update systems read of the space bar, each time they run, for the tests to compare with C++.
public static class KeyReadings
{
    private static readonly List<string> readings = new();
    private static readonly List<KeyEvent> events = new();

    // Each reading as "<phase> <just pressed> <held> <just released>", with 1 for true and 0 for false.
    internal static void Read(string phase) =>
        readings.Add($"{phase} {Bit(Keyboard.JustPressed(Key.Space))}{Bit(Keyboard.IsDown(Key.Space))}" +
                     $"{Bit(Keyboard.JustReleased(Key.Space))}");

    internal static void Received(KeyEvent keyEvent) => events.Add(keyEvent);

    public static void Clear()
    {
        readings.Clear();
        events.Clear();
    }

    // The readings so far, one per line.
    public static string Log() => string.Join("\n", readings);

    public static int EventCount() => events.Count;

    public static uint EventWindow(int index) => events[index].Window.Value;

    public static int EventKey(int index) => (int)events[index].Key;

    public static int EventScancode(int index) => (int)events[index].Scancode;

    public static int EventModifiers(int index) => (int)events[index].Modifiers;

    public static bool EventPressed(int index) => events[index].Pressed;

    public static bool EventRepeat(int index) => events[index].Repeat;

    // The same reads by scancode, and the modifiers, from outside any system.
    public static bool ScancodeIsDown(int scancode) => Keyboard.IsDown((Scancode)scancode);

    public static bool ScancodeJustPressed(int scancode) => Keyboard.JustPressed((Scancode)scancode);

    public static bool ScancodeJustReleased(int scancode) => Keyboard.JustReleased((Scancode)scancode);

    public static bool KeyIsDown(int key) => Keyboard.IsDown((Key)key);

    public static int Modifiers() => (int)Keyboard.Modifiers;

    private static char Bit(bool value) => value ? '1' : '0';
}

[System(Phase.Tick)]
public sealed class TickKeyReader : ISystem
{
    public void OnUpdate(in SystemContext context) => KeyReadings.Read("Tick");
}

[System(Phase.Update)]
public sealed class UpdateKeyReader : ISystem
{
    private readonly EventReader<KeyEvent> keys = new();

    public void OnUpdate(in SystemContext context)
    {
        KeyReadings.Read("Update");
        foreach (var keyEvent in keys.Read())
            KeyReadings.Received(keyEvent);
    }
}
