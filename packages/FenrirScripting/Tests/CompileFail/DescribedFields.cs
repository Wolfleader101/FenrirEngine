using System.Runtime.CompilerServices;
using Fenrir.Math;
using Fenrir.Scripting;

namespace CompileFail;

[InlineArray(4)]
public struct Text4
{
    private byte _element;
}

[InlineArray(3)]
public struct Floats3
{
    private float _element;
}

// A struct of the script's own holding another, beside one nested inside its component.
public struct Inner
{
    public int A;
}

public struct Outer
{
    public Inner Inner;
    public readonly int Fixed;
}

public enum Mode : byte
{
    Off,
    On,
}

// Every field the generated table measures: the table it writes must compile, readonly fields and all.
public unsafe struct Everything : IComponent
{
    public Entity Target;
    [FixedString] public fixed byte Label[8];
    [FixedString] public Text4 Short;
    public fixed float Weights[2];
    public Floats3 Spread;
    public Vector3 Offset;
    public Quaternion Turn;
    public Outer Outer;
    public Hidden Secret;
    public Mode Mode;
    public int* Next;
    public delegate* unmanaged<int, void> Callback;
    [Transient] public readonly long Frames;
    public nint Native;
    public char Letter;
    public bool Flag;
    public decimal Money;
    internal int NotDescribed;

    public struct Hidden
    {
        public float X;
    }
}

[Transient]
public struct Scratch : IComponent
{
    public Everything Copy;
}

[EditorOnly]
public struct Gizmo : IComponent
{
    public Inner Inner;
}
