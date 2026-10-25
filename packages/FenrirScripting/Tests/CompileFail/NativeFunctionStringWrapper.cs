using Fenrir.Scripting;

namespace CompileFail;

public struct Title
{
    public string Text;
}

// A struct of one field is a wrapper only if that field crosses by value, and a string does not.
public static partial class Titles
{
    [NativeFunction]
    public static partial int Show(Title title);
}
