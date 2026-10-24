using Fenrir.Scripting;

namespace CompileFail;

// An event is moved as bytes between C++ and C#, so it cannot hold a reference.
public struct BadEvent : IEvent
{
    public string Text;
}
