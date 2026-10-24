using Fenrir.Scripting;

namespace CompileFail;

// IComponent is named on the second part only. The generator must still find it, and see the field of the first part.
public partial struct Late
{
    public int A;
}

public partial struct Late : IComponent
{
    public int B;
}

public static class UseLate
{
    public static void Use(Ref<Late> late)
    {
        late.A += 1;
        late.B += 2;
    }
}
