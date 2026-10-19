using Fenrir.Physics;
using Fenrir.Scripting;

namespace CompileFail;

// Physics overwrites the state after every move, so C# can never write it.
public static class Controller
{
    public static void Use(Ref<CharacterBody> body)
    {
        body.State = new CharacterState();
    }
}
