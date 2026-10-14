using Fenrir.Scripting;

namespace CompileFail;

public struct Hit
{
    public float Distance;
    public int Id;
}

// Every way a value crosses to a native function: these must compile.
public static unsafe partial class Physics
{
    [NativeFunction]
    public static partial int Add(int left, int right);

    [NativeFunction]
    public static partial void Write(string text, ReadOnlySpan<char> more, int level);

    [NativeFunction]
    public static partial Entity Pick(Entity from, float range);

    [NativeFunction]
    public static partial bool Raycast(in Hit origin, ref Hit hit, out Hit last);

    [NativeFunction]
    public static partial nint Address(byte* data, char mark);

    [NativeFunction]
    public static partial void Tick();

    public static partial class Nested
    {
        [NativeFunction]
        internal static partial double Half(double value);
    }

    // The generated bodies are called like any method.
    public static int UseThem()
    {
        Write("text", "more".AsSpan(), 1);
        Hit hit = default;
        Raycast(in hit, ref hit, out Hit last);
        Tick();
        return Add(1, 2) + (int)Nested.Half(4) + (int)Pick(default, 1).Value + last.Id;
    }
}
