using System.Runtime.InteropServices;
using Fenrir.Scripting;

namespace CompileFail;

public enum CrateType : byte
{
    Wood,
    Metal,
}

public enum WideCrateType
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

// An int tag takes four bytes, so the kinds cannot start at 2.
[TaggedUnion]
[StructLayout(LayoutKind.Explicit)]
public partial struct Crate
{
    [FieldOffset(0)] private WideCrateType _type;
    [FieldOffset(2)] private Wood _wood;
    [FieldOffset(2)] private Metal _metal;
}
