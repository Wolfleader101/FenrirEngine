using Fenrir.Scripting;

namespace CompileFail;

// A readonly struct has no writable field to give out a ref to, so no accessor exists (and the generator warns).
public readonly struct Frozen : IComponent
{
    public readonly int A;
}

public static class UseFrozen
{
    public static int Use(Ref<Frozen> frozen) => frozen.A;
}
