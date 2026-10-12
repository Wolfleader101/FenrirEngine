using Fenrir.Scripting;

namespace CompileFail;

// A Ref<T> points into storage that can move, so it cannot outlive the call that was given it.
public class Keeper
{
    private Ref<Probe> kept;
}
