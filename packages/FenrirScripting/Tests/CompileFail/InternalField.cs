using Fenrir.Scripting;

namespace CompileFail;

internal struct Secret
{
    public int N;
}

// A public component with an internal field of an internal type: the accessor for it must not be public, or the
// generated code would not compile.
public struct Mixed : IComponent
{
    public int Open;
    internal Secret Hidden;
}

public static class UseMixed
{
    public static void Use(Ref<Mixed> mixed)
    {
        mixed.Open += 1;
        mixed.Hidden.N += 1;
    }
}
