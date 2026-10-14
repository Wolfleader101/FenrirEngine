using Fenrir.Scripting;

namespace CompileFail;

// A native function inside a generic type: one C++ function cannot serve every instantiation. The build must report only
// that, not also a missing implementation of the partial method.
public static partial class Box<T> where T : unmanaged
{
    [NativeFunction]
    public static partial int Add(int left, int right);
}
