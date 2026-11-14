using Fenrir.Ragdoll;
using Fenrir.Scripting;

namespace CompileFail;

// Nor any field of it.
public static class Ragdolls
{
    public static void Use(Ref<RagdollBody> body)
    {
        body.State.Bone = 3;
    }
}
