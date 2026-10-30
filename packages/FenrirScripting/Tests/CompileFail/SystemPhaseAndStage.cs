using Fenrir.Scripting;

namespace CompileFail;

// A system runs in a phase or in a stage, never both: the generator refuses one that asks for both.
[System(Phase.Update)]
[StageSystem("CompileFail.Somewhere.Stage")]
public sealed class PhaseAndStage : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
    }
}
