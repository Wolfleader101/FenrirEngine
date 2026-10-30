using Fenrir.Scripting;

namespace Fenrir.TestExtension;

// The native functions of the test extension, declared once: the generator writes each body and the field the host binds
// to the C++ function the test's extension declares as "Fenrir.TestExtension.Gadgets.<Method>".
public static partial class Gadgets
{
    [NativeFunction]
    public static partial int Add(int left, int right);

    // An entity crosses as C++'s Fenrir::Entity, by pointer: `in` to the native, `out` back.
    [NativeFunction]
    public static partial void EchoEntity(in Entity entity, out Entity echoed);

    // The entity's Transform height, as native code reads it; -1 if it has none.
    [NativeFunction]
    public static partial float HeightOf(in Entity entity);

    [NativeFunction]
    public static partial void Lift(in Entity entity, float by);

    // The UTF-8 byte count of the text, times ten, plus the number; the native side keeps the text it received.
    [NativeFunction]
    public static partial int Label(string text, int number);

    // Creates an entity through the change buffer and gives it a Charge of `level` in the same call.
    [NativeFunction]
    public static partial void SpawnCharged(int level, out Entity entity);

    // Records, in this order: Charge 1 on `kept`, its removal, Charge 3 on it, destroying `doomed`, Charge 9 on `doomed`
    // (skipped at playback), and a Weather of `wind` in the active scene.
    [NativeFunction]
    public static partial void Rearrange(in Entity kept, in Entity doomed, float wind);

    // Creates an entity through the change buffer and records parenting it under `parent`.
    [NativeFunction]
    public static partial void SpawnUnder(in Entity parent, out Entity child);

    // Adds to the running total the extension keeps in its extension state, and returns the new total.
    [NativeFunction]
    public static partial int Tally(int by);

    // The next heading clockwise: an enum crosses as its underlying byte.
    [NativeFunction]
    public static partial Heading TurnRight(Heading heading);

    // The tag with its code raised by `by` and the heading's value: a wrapper crosses as its one field.
    [NativeFunction]
    public static partial Tag Retag(Tag tag, Heading by);

    // Raises the entity's Transform by one, behind the native guard: a dead entity warns once under the native's name.
    [NativeFunction]
    public static partial void Nudge(in Entity entity);

    // One reading of the entity (a count of one and its height), through the registration that zeroes `reading` before
    // the native runs: zero readings, and a zeroed `reading`, for a dead entity; -1 when no Scripting runs.
    [NativeFunction]
    public static partial int Weigh(in Entity entity, out Reading reading);

    // Adds the text to the trace the extension keeps in its extension state, beside what its bench's C++ items add.
    [NativeFunction]
    public static partial void Trace(string text);
}

/// <summary>The extension stage the test extension hosts: its bench, C++ items by order that stage systems join.</summary>
public static class Bench
{
    public const string Stage = "Fenrir.TestExtension.Bench";
}

/// <summary>A heading, which the test extension's natives take and return by value.</summary>
public enum Heading : byte
{
    North = 1,
    East = 2,
    South = 3,
    West = 4,
}

/// <summary>A wrapper of the test extension: a struct of one field, which crosses to a native as that field.</summary>
public struct Tag
{
    public ushort Code;
}

/// <summary>A scene resource the test extension gives C#: the native <c>Weather</c>.</summary>
[NativeResource("Fenrir.TestExtension.Weather")]
public struct Weather : IResource
{
    public float Wind;
    public int Gusts;
}

/// <summary>An app resource the test extension gives C#: the native <c>Settings</c>.</summary>
[NativeResource("Fenrir.TestExtension.Settings")]
public struct Settings : IResource
{
    public int Volume;
}

/// <summary>A read-only app resource the test extension gives C#: the native <c>Clock</c>.</summary>
[NativeResource("Fenrir.TestExtension.Clock")]
public struct Clock : IReadOnlyResource
{
    public double Seconds;
}

/// <summary>A shared struct of the test extension: plain data its natives would take by pointer.</summary>
public struct Reading
{
    public int Count;
    public float Weight;
}

/// <summary>A component the test extension gives C#: the native <c>Charge</c>.</summary>
[NativeComponent("Fenrir.TestExtension.Charge")]
public struct Charge : IComponent
{
    public int Level;
}

/// <summary>An event the test extension shares with C#: the native <c>Ping</c>.</summary>
[NativeEvent("Fenrir.TestExtension.Ping")]
public struct Ping : IEvent
{
    public int Value;
}
