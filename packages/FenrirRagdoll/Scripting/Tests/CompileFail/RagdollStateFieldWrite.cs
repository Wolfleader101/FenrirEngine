using Fenrir.Ragdoll;
using Fenrir.Scripting;

namespace CompileFail;

// Nor any field of it.
public static class Ragdolls
{
    public static void Use(Ref<Ragdoll> ragdoll)
    {
        ragdoll.State.AnyAwake = true;
    }
}
