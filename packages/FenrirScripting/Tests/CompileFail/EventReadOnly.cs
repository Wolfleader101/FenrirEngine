using Fenrir.Scripting;

namespace CompileFail;

public struct Whisper : IEvent
{
    public int Volume;
}

// What a system reads is a view of the engine's queue: it cannot be written through.
public static class Tamper
{
    public static void Rewrite(in SystemContext context)
    {
        context.Events.Read<Whisper>()[0].Volume = 11;
    }
}
