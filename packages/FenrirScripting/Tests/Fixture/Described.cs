using System.Runtime.CompilerServices;
using Fenrir.Math;
using Fenrir.Scripting;

namespace Fenrir.ScriptFixture;

// Eight bytes of UTF-8 text as an [InlineArray] struct, the other form a [FixedString] field takes.
[InlineArray(8)]
public struct Tag8
{
    private byte _element;
}

// A struct a component holds by value, described as a nested type of its own.
public struct Spot
{
    public float Height;
    public int Count;
}

// A component with a field of each kind the engine describes from C#: an entity, two fixed strings, an engine struct
// (Vector3, described as Math::Vec3), a struct of the script's own, a transient field, a fixed array, and an internal
// field, which no file holds. Its layout is sequential, so the offsets the engine measures are the ones written beside
// each field.
public unsafe struct Described : IComponent
{
    public Entity Target;                         // 0, 16 bytes
    [FixedString] public fixed byte Label[16];    // 16
    [FixedString] public Tag8 Short;              // 32
    public Vector3 Offset;                        // 40
    public Spot Spot = new() { Height = 2, Count = 3 }; // 52
    [Transient] public int Frames;                // 60
    public fixed int Slots[2];                    // 64
    internal int Hidden;                          // 72; the struct is 80, a multiple of Entity's 8

    public Described()
    {
    }
}

// A component that is never written to a file, and one only an editor reads.
[Transient]
public struct Scratch : IComponent
{
    public int Count;
}

[EditorOnly]
public struct Gizmo : IComponent
{
    public float Size;
}
