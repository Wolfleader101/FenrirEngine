using Fenrir.Scripting;
using Num = System.Int32;

namespace CompileFail;

public static partial class Gadgets
{
    [NativeFunction]
    public static partial Num Add(Num left, Num right);

    public static long UseBoth() => Add(1, 2) + AddWide(3, 4);
}
