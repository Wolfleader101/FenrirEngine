using Fenrir.Scripting;

namespace CompileFail;

// A native function is a C++ function with no C# instance to call it on.
public partial class Gadget
{
    [NativeFunction]
    public partial int Add(int left, int right);
}
