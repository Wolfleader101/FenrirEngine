using Fenrir.Scripting;

namespace CompileFail;

// The generator writes a native function's body, so the declaration has none.
public static partial class Gadget
{
    [NativeFunction]
    public static int Add(int left, int right) => left + right;
}
