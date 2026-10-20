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

// Nor to one inside a class that is not partial.
public static class Store
{
    [TaggedUnion]
    [StructLayout(LayoutKind.Explicit)]
    public partial struct Crate
    {
        [FieldOffset(0)] private CrateType _type;
        [FieldOffset(4)] private Wood _wood;
        [FieldOffset(4)] private Metal _metal;
    }
}
