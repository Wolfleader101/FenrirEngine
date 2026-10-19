using Fenrir.Scripting;

namespace CompileFail;

// A readonly field of a writable component is readable through Ref<T> but never writable.
public struct Meter : IComponent
{
    public int Speed;
    public readonly int Samples;
}

public static class WriteMeter
{
    public static void Use(Ref<Meter> meter)
    {
        meter.Samples = 1;
    }
}
