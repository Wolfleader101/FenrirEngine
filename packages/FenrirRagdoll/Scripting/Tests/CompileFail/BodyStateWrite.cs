using Fenrir.Ragdoll;
using Fenrir.Scripting;

namespace CompileFail;

// The glue overwrites a body's state every tick, so C# can never write it.
public static class Ragdolls
{
    public static void Use(Ref<RagdollBody> body)
    {
        body.State = new RagdollBodyState();
    }
}
