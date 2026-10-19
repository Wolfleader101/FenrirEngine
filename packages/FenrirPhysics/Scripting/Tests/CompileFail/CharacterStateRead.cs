using Fenrir.Physics;
using Fenrir.Scripting;

namespace CompileFail;

// A controller holds Ref<CharacterBody> to write Velocity, and reads State through the same reference, in place.
public static class Controller
{
    public static bool Use(Ref<CharacterBody> body)
    {
        body.Velocity.Y = body.State.Ground == GroundState.OnGround ? 5 : body.Velocity.Y;
        ref readonly CharacterState state = ref body.State;
        return state.ShapeBlocked || !body.State.GroundBody.IsNull;
    }

    public static GroundState Look(ReadOnlyRef<CharacterBody> body) => body.State.Ground;
}
