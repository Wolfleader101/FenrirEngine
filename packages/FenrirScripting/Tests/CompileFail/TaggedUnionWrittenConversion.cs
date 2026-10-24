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

// The generator writes each conversion.
[TaggedUnion]
[StructLayout(LayoutKind.Explicit)]
public partial struct Crate
{
    [FieldOffset(0)] private CrateType _type;
    [FieldOffset(4)] private Wood _wood;
    [FieldOffset(4)] private Metal _metal;

    public static implicit operator Crate(Metal value) => new() { _type = CrateType.Metal, _metal = value };
}
