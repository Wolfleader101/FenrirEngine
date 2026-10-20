using System.Runtime.InteropServices;
using Fenrir.Scripting;

namespace CompileFail;

// An int tag, as the scripting tests' Shape has, and no noun: the message names the struct.
public enum ReadingType
{
    Whole,
    Fraction,
}

public struct Whole
{
    public int Value;
}

public struct Fraction
{
    public float Value;
}

[TaggedUnion]
[StructLayout(LayoutKind.Explicit)]
public partial struct Reading
{
    [FieldOffset(0)] private ReadingType _type;
    [FieldOffset(4)] private Whole _whole;
    [FieldOffset(4)] private Fraction _fraction;
}

public static class UseReading
{
    public static float Use()
    {
        Reading reading = new Fraction { Value = 0.5f };
        return reading.Type == ReadingType.Fraction ? reading.Fraction.Value : new Reading().Whole.Value;
    }
}
