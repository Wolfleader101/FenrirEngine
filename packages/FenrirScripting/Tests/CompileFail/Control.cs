using Fenrir.Scripting;

namespace CompileFail;

// Everything a script may do with a Ref<T>: take it as a parameter, use it, pass it down. This must compile, so the
// failing cases below fail because of Ref<T>'s rules and not because the project is set up wrongly.
public static class Control
{
    public static void Use(Ref<Probe> probe)
    {
        probe.Count += 1;
        Bump(probe);
    }

    private static void Bump(Ref<Probe> probe) => probe.Count *= 2;
}
