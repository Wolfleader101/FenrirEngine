using Fenrir.Scripting;

namespace CompileFail;

// A read-only resource is read through ReadOnlyRef<T>, and assigning through it is refused.
[NativeResource("CompileFail.Clock")]
public struct Clock : IReadOnlyResource
{
    public double Seconds;
}

public static class WriteClock
{
    public static void Use()
    {
        var clock = Resources.GetReadOnly<Clock>();
        clock.Seconds = 1;
    }
}
