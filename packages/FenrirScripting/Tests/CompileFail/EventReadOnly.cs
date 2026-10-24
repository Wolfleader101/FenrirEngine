using Fenrir.Scripting;

namespace CompileFail;

public struct Whisper : IEvent
{
    public int Volume;
}

// What a system reads is a view of the engine's queue: it cannot be written through.
public static class Tamper
{
    public static void Rewrite(EventReader<Whisper> reader)
    {
        reader.Read()[0].Volume = 11;
    }
}
