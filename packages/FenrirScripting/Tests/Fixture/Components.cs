using Fenrir.Scripting;

namespace Fenrir.ScriptFixture;

public struct Vec3
{
    public float X, Y, Z;
}

// A component with a nested struct, so the tests cover t.Position.X += 1.
public struct Transform : IComponent
{
    public Vec3 Position;
    public float Scale;
    public int Flags;
}

public static class Holder
{
    // A nested component gets its own accessors class.
    public struct Nested : IComponent
    {
        public double Weight;
    }
}

internal struct Hidden : IComponent
{
    public int Count;
}

// Fields named like members of Ref<T> are not exposed (the generator warns); the others still are.
public struct Clash : IComponent
{
    public int Value;
    public int IsNull;
    public int Fine;
}

// The keyword-named field needs an escape in the generated code.
public struct Reserved : IComponent
{
    public int @class;
    public int @event;
}

// A struct split over two declarations still gets one accessors class, with every field. The compiler warns that the
// layout order is then undefined (CS0282), which a real component must avoid; the test accepts it on purpose.
#pragma warning disable CS0282
public partial struct Split : IComponent
{
    public int A;
}

public partial struct Split
{
    public int B;
}
#pragma warning restore CS0282

// Reads and writes native memory the test owns, through Ref<T> and the generated accessors, from the script side.
public static unsafe class Access
{
    public static void Nudge(ulong address)
    {
        var t = new Ref<Transform>((Transform*)address);
        t.Position.X += 1;
        t.Position.Y = 5;
        t.Scale *= 2;
        t.Flags |= 4;
    }

    public static float ReadSum(ulong address)
    {
        var t = new Ref<Transform>((Transform*)address);
        return t.Position.X + t.Position.Y + t.Position.Z + t.Scale;
    }

    public static double NudgeNested(ulong address)
    {
        var t = new Ref<Holder.Nested>((Holder.Nested*)address);
        t.Weight += 0.5;
        return t.Weight;
    }

    public static int NudgeHidden(ulong address)
    {
        var t = new Ref<Hidden>((Hidden*)address);
        t.Count += 3;
        return t.Count;
    }

    // Value is Ref<T>.Value (the whole component); Fine is the generated accessor.
    public static int NudgeClash(ulong address)
    {
        var t = new Ref<Clash>((Clash*)address);
        t.Fine = 7;
        t.Value.Value = 8;
        t.Value.IsNull = 9;
        return t.Fine + t.Value.Value + t.Value.IsNull;
    }

    public static int NudgeSplit(ulong address)
    {
        var t = new Ref<Split>((Split*)address);
        t.A += 1;
        t.B += 2;
        return t.A + t.B;
    }

    public static int NudgeReserved(ulong address)
    {
        var t = new Ref<Reserved>((Reserved*)address);
        t.@class += 1;
        t.@event += 2;
        return t.@class + t.@event;
    }

    // A null Ref says so, and does not crash the host.
    public static string? NullRefMessage()
    {
        try
        {
            var t = new Ref<Transform>(null);
            _ = t.Value;
            return null;
        }
        catch (InvalidOperationException exception)
        {
            return exception.Message;
        }
    }

    public static bool NullRefIsNull() => new Ref<Transform>(null).IsNull;
}
