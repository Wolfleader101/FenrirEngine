using Fenrir.Scripting;

namespace CompileFail;

// The engine makes each system with its parameterless constructor, so one it cannot call is an error at build time.
[System(Phase.Update)]
public sealed class Unreachable : ISystem
{
    private Unreachable()
    {
    }

    public void OnUpdate(in SystemContext context)
    {
    }
}
