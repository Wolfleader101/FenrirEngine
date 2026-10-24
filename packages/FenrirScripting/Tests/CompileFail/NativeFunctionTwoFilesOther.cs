using Fenrir.Scripting;
using Num = System.Int64;

namespace CompileFail;

public static partial class Gadgets
{
    [NativeFunction]
    public static partial Num AddWide(Num left, Num right);
}
