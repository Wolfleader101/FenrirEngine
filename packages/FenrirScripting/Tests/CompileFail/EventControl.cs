using Fenrir.Scripting;

namespace CompileFail;

public struct Noise : IEvent
{
    public int Loudness;
}

// Sending and reading an event from a system, through a reader it keeps in a field. This must compile.
[System(Phase.Update)]
public sealed class Listener : ISystem
{
    private readonly EventReader<Noise> noises = new();

    public void OnUpdate(in SystemContext context)
    {
        foreach (var noise in noises.Read())
        {
            if (noise.Loudness > 10)
                context.Events.Send(new Noise { Loudness = noise.Loudness - 1 });
        }
    }
}
