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

// A kind holding a reference cannot live in native storage.
public struct Label
{
    public string Text;
}

[TaggedUnion]
[StructLayout(LayoutKind.Explicit)]
public partial struct Crate
{
    [FieldOffset(0)] private CrateType _type;
    [FieldOffset(4)] private Wood _wood;
    [FieldOffset(4)] private Label _metal;
}
