using Fenrir.Scripting;

namespace CompileFail;

public static class LocalWaiter
{
    // Not a parameter this time: a local that is still alive after the await.
    public static async Task Wait()
    {
        Ref<Probe> held = default;
        await Task.Delay(1);
        _ = held.IsNull;
    }
}
