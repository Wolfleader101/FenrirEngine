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

// Two enums at offset 0: which is the tag?
[TaggedUnion]
[StructLayout(LayoutKind.Explicit)]
public partial struct Crate
{
    [FieldOffset(0)] private CrateType _type;
    [FieldOffset(0)] private CrateType _other;
    [FieldOffset(4)] private Wood _wood;
    [FieldOffset(4)] private Metal _metal;
}
