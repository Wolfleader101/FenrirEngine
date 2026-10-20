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

// The generator cannot add to a struct that is not partial.
[TaggedUnion]
[StructLayout(LayoutKind.Explicit)]
public struct Crate
{
    [FieldOffset(0)] private CrateType _type;
    [FieldOffset(4)] private Wood _wood;
    [FieldOffset(4)] private Metal _metal;
}
