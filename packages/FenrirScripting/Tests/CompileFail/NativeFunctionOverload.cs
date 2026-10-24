using Fenrir.Scripting;

namespace CompileFail;

// A native function is named by its type and method name alone.
public static partial class Gadget
{
    [NativeFunction]
    public static partial int Add(int left, int right);

    [NativeFunction]
    public static partial long Add(long left, long right);
}
