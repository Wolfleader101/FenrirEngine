using Fenrir.Scripting;

namespace CompileFail;

// A read-only resource has no Ref<T>: Resources.Get, Insert and Remove are only for an IResource.
[NativeResource("CompileFail.Clock")]
public struct Clock : IReadOnlyResource
{
    public double Seconds;
}

public static class GetClock
{
    public static void Use()
    {
        var clock = Resources.Get<Clock>();
    }
}
