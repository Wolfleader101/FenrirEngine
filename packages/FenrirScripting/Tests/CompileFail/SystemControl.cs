using Fenrir.Scripting;

namespace CompileFail;

// A well-formed system, using the context's query entry point. This must compile.
[System(Phase.Update, Order = 1)]
public sealed class WellFormed : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        foreach (var (entity, probe) in context.Query<Probe>())
            probe.Count += 1;
    }
}
