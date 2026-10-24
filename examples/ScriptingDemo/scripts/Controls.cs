using Fenrir.Input;
using Fenrir.Scripting;

// Outside any Fenrir.* namespace, as a game's scripts are.
namespace ScriptingDemo.Scripts;

// EVENTS THAT COME FROM C++, AND EVENTS THAT GO TO IT
//
// FenrirInput sends a KeyEvent from C++ for every key that goes down or up. Its scripting extension shares it as
// Fenrir.Input.KeyEvent, read here unchanged. Burst is the other way round: C# sends it, and both C# (Sparks.cs) and C++
// (src/Showcase.cpp, LogBursts) read it. In both cases there is one queue, and each reader, in either language, sees each event
// once.

// Declared in C++ (examples/ScriptingDemo/src/Showcase.hpp) and registered by the app as "ScriptingDemo.Burst".
[NativeEvent("ScriptingDemo.Burst")]
public struct Burst : IEvent
{
    public int Count;
}

// R reverses every spin. Space starts a burst of sparks.
[System(Phase.Update, Order = 5)]
public sealed class Controls : ISystem
{
    // Remembers which key events this system has already handled, so each is handled once.
    private readonly EventReader<KeyEvent> keys = new();

    public void OnUpdate(in SystemContext context)
    {
        foreach (var key in keys.Read())
        {
            // A press, not a release or the repeats of a held key.
            if (!key.Pressed || key.Repeat)
                continue;

            if (key.Key == Key.R)
            {
                foreach (var (entity, spin) in context.Query<Spin>())
                    spin.RadiansPerSecond = -spin.RadiansPerSecond;
                context.Log.Info("R: reversed the spin.");
            }
            else if (key.Key == Key.Space)
            {
                // Held until this system returns, then delivered to the C# Spawner and to C++ alike.
                context.Events.Send(new Burst { Count = 5 });
            }
        }
    }
}
