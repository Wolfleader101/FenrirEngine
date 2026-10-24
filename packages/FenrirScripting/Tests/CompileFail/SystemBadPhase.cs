using Fenrir.Scripting;

namespace CompileFail;

// A phase that does not exist would be scheduled nowhere: the generator refuses it.
[System((Phase)9)]
public sealed class NoSuchPhase : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
    }
}
