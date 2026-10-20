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

// A ref struct cannot be stored in a component.
[TaggedUnion]
[StructLayout(LayoutKind.Explicit)]
public ref partial struct Crate
{
    [FieldOffset(0)] private CrateType _type;
    [FieldOffset(4)] private Wood _wood;
    [FieldOffset(4)] private Metal _metal;
}
