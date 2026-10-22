using Fenrir.Scripting;

namespace CompileFail;

// Everything a script may do with native resources: read and write a writable one through Ref<T>, read a read-only one
// through ReadOnlyRef<T>, ask whether one is there, and insert and remove a writable one. This must compile, so the
// failing resource cases fail because of the rules and not because the project is set up wrongly.
[NativeResource("CompileFail.Weather")]
public struct Weather : IResource
{
    public float Wind;
}

[NativeResource("CompileFail.Clock")]
public struct Clock : IReadOnlyResource
{
    public double Seconds;
}

public static class UseResources
{
    public static double Use()
    {
        var weather = Resources.Get<Weather>();
        weather.Wind += 1;
        if (Resources.TryGet<Weather>(out var again))
            again.Wind *= 2;
        Resources.Insert(new Weather { Wind = 3 });
        Resources.Insert<Weather>();
        Resources.Remove<Weather>();
        double seconds = Resources.GetReadOnly<Clock>().Seconds;
        return Resources.TryGetReadOnly<Clock>(out var clock) ? clock.Seconds + seconds : seconds;
    }
}
