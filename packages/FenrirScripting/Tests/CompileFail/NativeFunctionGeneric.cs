using Fenrir.Scripting;

namespace CompileFail;

// One C++ function cannot serve every instantiation.
public static partial class Gadget
{
    [NativeFunction]
    public static partial T Echo<T>(T value) where T : unmanaged;
}
