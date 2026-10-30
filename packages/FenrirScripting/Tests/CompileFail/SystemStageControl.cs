using Fenrir.Scripting;

namespace CompileFail;

// A well-formed stage system, joining a stage by its full name at an order. This must compile: whether the stage is
// declared is the engine's check, when the script loads.
[StageSystem("CompileFail.Somewhere.Stage", Order = 15)]
public sealed class WellFormedStage : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
    }
}
