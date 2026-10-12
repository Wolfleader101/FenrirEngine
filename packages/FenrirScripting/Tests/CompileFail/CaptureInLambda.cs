using Fenrir.Scripting;

namespace CompileFail;

public static class Capturer
{
    public static Action Capture(Ref<Probe> probe)
    {
        return () => probe.Count += 1;
    }
}
