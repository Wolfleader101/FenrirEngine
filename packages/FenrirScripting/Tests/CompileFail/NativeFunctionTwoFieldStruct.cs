using Fenrir.Scripting;

namespace CompileFail;

public struct Span2
{
    public int Start;
    public int Length;
}

// A struct of two fields is not a wrapper: it crosses only by in/ref/out.
public static partial class Ranges
{
    [NativeFunction]
    public static partial int Measure(Span2 range);
}
