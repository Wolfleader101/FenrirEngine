using Fenrir.Scripting;

namespace CompileFail;

// A wrapper the native function's type can read but cannot build: no constructor taking its number, and no setter.
public readonly struct Serial
{
    public static Serial First => default;
    public uint Number { get; }
}

public static partial class Serials
{
    [NativeFunction]
    public static partial Serial Next();
}
