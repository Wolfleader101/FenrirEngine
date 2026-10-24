using Fenrir.Scripting;

// One type declared in two namespace blocks of one file, each with its own using alias. Each block's generated part must
// mean what its own block says, and the two parts need names of their own.
namespace CompileFail
{
    using Num = System.Int32;

    public static partial class Gadgets
    {
        [NativeFunction]
        public static partial Num Add(Num left, Num right);
    }
}

namespace CompileFail
{
    using Num = System.Int64;

    public static partial class Gadgets
    {
        [NativeFunction]
        public static partial Num AddWide(Num left, Num right);

        public static long UseBoth() => Add(1, 2) + AddWide(3, 4);
    }
}
