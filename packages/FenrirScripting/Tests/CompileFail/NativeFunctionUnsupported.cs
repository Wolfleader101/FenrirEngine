using Fenrir.Scripting;

namespace CompileFail;

// An object cannot cross to C++.
public static partial class Gadget
{
    [NativeFunction]
    public static partial int Count(object things);
}
