using System.Runtime.InteropServices;
using Fenrir.Scripting;

namespace CompileFail;

// new() makes the kind with tag value 0, and there is none.
public enum BoxType : byte
{
    Wood = 1,
    Metal = 2,
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
