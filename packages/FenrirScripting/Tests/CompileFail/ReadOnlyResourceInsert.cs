using Fenrir.Scripting;

namespace CompileFail;

// A read-only resource cannot be inserted from C#: only the engine makes it.
[NativeResource("CompileFail.Clock")]
public struct Clock : IReadOnlyResource
{
    public double Seconds;
}

public static class InsertClock
{
    public static void Use()
    {
        Resources.Insert(new Clock());
    }
}
