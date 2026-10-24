using System.Runtime.InteropServices;
using Fenrir.Scripting;

namespace CompileFail;

// Two tag members with one value: the message could not say which it is.
public enum BoxType : byte
{
    Wood,
    Metal = 0,
}

public struct Wood
{
    public float Grain;
}

public struct Metal
{
    public int Gauge;
}

[TaggedUnion]
[StructLayout(LayoutKind.Explicit)]
public partial struct Box
{
    [FieldOffset(0)] private BoxType _type;
    [FieldOffset(4)] private Wood _wood;
    [FieldOffset(4)] private Metal _metal;
}
