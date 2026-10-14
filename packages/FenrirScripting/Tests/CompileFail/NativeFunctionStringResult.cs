using Fenrir.Scripting;

namespace CompileFail;

// A string can be passed to C++, but not returned from it.
public static partial class Gadget
{
    [NativeFunction]
    public static partial string Name();
}
