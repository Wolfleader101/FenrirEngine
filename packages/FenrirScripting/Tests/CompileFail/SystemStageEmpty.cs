using Fenrir.Scripting;

namespace CompileFail;

// A stage system with no stage name could never be placed: the generator refuses it.
[StageSystem("")]
public sealed class NoStage : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
    }
}
