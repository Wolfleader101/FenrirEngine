using Fenrir.Ragdoll;
using Fenrir.Scripting;

namespace CompileFail;

// The glue overwrites a ragdoll's state every tick, so C# can never write it.
public static class Ragdolls
{
    public static void Use(Ref<Ragdoll> ragdoll)
    {
        ragdoll.State = new RagdollState();
    }
}
