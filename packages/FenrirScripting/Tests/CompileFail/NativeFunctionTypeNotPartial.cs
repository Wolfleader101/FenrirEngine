using Fenrir.Scripting;

namespace CompileFail;

// The generator adds the body and the field to the type, and to every type around it, so they must all be partial.
public static class Outer
{
    public static partial class Inner
    {
        [NativeFunction]
        public static partial int Add(int left, int right);
    }
}
