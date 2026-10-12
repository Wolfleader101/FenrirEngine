using Fenrir.Scripting;

namespace CompileFail;

public static class Waiter
{
    public static async Task Wait(Ref<Probe> probe)
    {
        await Task.Delay(1);
        probe.Count += 1;
    }
}
