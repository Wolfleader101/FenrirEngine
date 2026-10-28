using Fenrir.Scripting;

namespace CompileFail;

public struct Hit
{
    public float Distance;
    public int Id;
}

public enum Heading : byte { North, East }

// Wrappers: a struct of one field or auto-property crosses as it. Badge is built back with an object initializer, Weight
// through its init accessor, Facing (a wrapper of an enum) likewise, and Slot through its constructor.
public struct Badge
{
    public ushort Code;
}

public struct Weight
{
    public float Kilograms { get; init; }
}

public struct Facing
{
    public Heading Way;
}

public readonly struct Slot
{
    public Slot(int index) => Index = index;
    public int Index { get; }
}

// Every way a value crosses to a native function: these must compile.
public static unsafe partial class Physics
{
    [NativeFunction]
    public static partial int Add(int left, int right);

    [NativeFunction]
    public static partial void Write(string text, ReadOnlySpan<char> more, int level);

    [NativeFunction]
    public static partial bool Pick(in Entity from, float range, out Entity picked);

    [NativeFunction]
    public static partial bool Raycast(in Hit origin, ref Hit hit, out Hit last);

    [NativeFunction]
    public static partial nint Address(byte* data, char mark);

    [NativeFunction]
    public static partial void Tick();

    [NativeFunction]
    public static partial Heading Turn(Heading heading, Badge badge);

    [NativeFunction]
    public static partial Badge Rebadge(Weight weight, Facing facing, Slot slot);

    [NativeFunction]
    public static partial Weight Weigh(Badge badge);

    [NativeFunction]
    public static partial Facing Face(Heading heading);

    [NativeFunction]
    public static partial Slot Next(Slot slot);

    public static partial class Nested
    {
        [NativeFunction]
        internal static partial double Half(double value);
    }

    // The generated bodies are called like any method.
    public static int UseThem()
    {
        Write("text", "more".AsSpan(), 1);
        Hit hit = default;
        Raycast(in hit, ref hit, out Hit last);
        Pick(default, 1, out Entity picked);
        Tick();
        Badge badge = Rebadge(Weigh(new Badge { Code = 2 }), Face(Turn(Heading.North, default)), Next(new Slot(1)));
        return badge.Code + Add(1, 2) + (int)Nested.Half(4) + (int)picked.Value + last.Id;
    }
}
