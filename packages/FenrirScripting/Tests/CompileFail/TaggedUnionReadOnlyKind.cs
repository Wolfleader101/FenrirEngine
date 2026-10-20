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

// A view cannot be a writable reference to a readonly field.
[TaggedUnion]
[StructLayout(LayoutKind.Explicit)]
public partial struct Crate
{
    [FieldOffset(0)] private CrateType _type;
    [FieldOffset(4)] private Wood _wood;
    [FieldOffset(4)] private readonly Metal _metal;
}
