using Fenrir.Physics;
using Fenrir.Scripting;

namespace CompileFail;

// Nor any field of it.
public static class Controller
{
    public static void Use(Ref<CharacterBody> body)
    {
        body.State.Ground = GroundState.OnGround;
    }
}
