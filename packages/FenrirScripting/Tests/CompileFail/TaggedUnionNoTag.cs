using System.Runtime.InteropServices;
using Fenrir.Scripting;

namespace CompileFail;

public enum CrateType : byte
{
    Wood,
    Metal,
}

public struct Wood
{
    public float Grain;
}

public struct Metal
{
    public int Gauge;
}

// The tag is not at offset 0.
[TaggedUnion]
[StructLayout(LayoutKind.Explicit)]
public partial struct Crate
{
    [FieldOffset(0)] private int _count;
    [FieldOffset(4)] private CrateType _type;
    [FieldOffset(8)] private Wood _wood;
    [FieldOffset(8)] private Metal _metal;
}
