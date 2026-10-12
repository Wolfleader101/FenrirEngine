using Fenrir.Scripting;

namespace CompileFail;

public static class Collector
{
    public static void Collect(Ref<Probe> probe)
    {
        var all = new List<Ref<Probe>>();
        all.Add(probe);
    }
}
