using Fenrir.Scripting;

namespace CompileFail;

// A component lives in native memory, so it cannot hold a reference. The generator says so, instead of emitting code
// that fails somewhere inside generated text.
public struct Bad : IComponent
{
    public string Name;
}
