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

// A class is not a union.
[TaggedUnion]
[StructLayout(LayoutKind.Explicit)]
public partial class Crate
{
    [FieldOffset(0)] public CrateType _type;
    [FieldOffset(4)] public Wood _wood;
    [FieldOffset(4)] public Metal _metal;
}
