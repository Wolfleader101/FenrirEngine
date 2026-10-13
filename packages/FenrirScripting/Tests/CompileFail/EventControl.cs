using Fenrir.Scripting;

namespace CompileFail;

public struct Noise : IEvent
{
    public int Loudness;
}

// Sending and reading an event from a system. This must compile.
[System(Phase.Update)]
public sealed class Listener : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        foreach (var noise in context.Events.Read<Noise>())
        {
            if (noise.Loudness > 10)
                context.Events.Send(new Noise { Loudness = noise.Loudness - 1 });
        }
    }
}
