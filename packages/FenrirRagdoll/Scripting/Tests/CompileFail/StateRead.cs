using Fenrir.Ragdoll;
using Fenrir.Scripting;

namespace CompileFail;

// A ragdoll system holds Ref<Ragdoll> to write Mode, and reads State through the same reference, in place; a body's
// likewise.
public static class Ragdolls
{
    public static bool Use(Ref<Ragdoll> ragdoll, Ref<RagdollBody> body)
    {
        ragdoll.Mode = ragdoll.State.AnyAwake ? RagdollMode.Simulating : ragdoll.Mode;
        ref readonly RagdollState state = ref ragdoll.State;
        body.Mode = body.State.Mode == RagdollMode.Following ? RagdollBodyMode.Simulating : body.Mode;
        ref readonly RagdollBodyState bodyState = ref body.State;
        return state.Inactive || bodyState.Bone >= 0;
    }

    public static int Look(ReadOnlyRef<Ragdoll> ragdoll) => ragdoll.State.Bodies;
}
