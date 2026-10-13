using Fenrir.Scripting;

namespace CompileFail;

// [System] on a class that is not an ISystem would silently never run: the generator refuses it.
[System(Phase.Update)]
public sealed class NotOne
{
}
