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

// A readonly struct cannot hand out writable views.
[TaggedUnion]
[StructLayout(LayoutKind.Explicit)]
public readonly partial struct Crate
{
    [FieldOffset(0)] private readonly CrateType _type;
    [FieldOffset(4)] private readonly Wood _wood;
    [FieldOffset(4)] private readonly Metal _metal;
}
