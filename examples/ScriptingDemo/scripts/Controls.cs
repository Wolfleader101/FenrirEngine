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

// R reverses every spin, and [ and ] slow it and speed it up, through the SpinDial (Spin.cs) the Razor panel shows.
// Space starts a burst of sparks.
[System(Phase.Update, Order = 5)]
public sealed class Controls : ISystem
{
    // What [ and ] divide and multiply the spin's speed by.
    private const float SpeedStep = 1.5f;

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
                foreach (var (_, dial) in context.Query<SpinDial>())
                    dial.Reversed = dial.Reversed != 0 ? 0 : 1;
                context.Log.Info("R: reversed the spin.");
            }
            else if (key.Key is Key.LeftBracket or Key.RightBracket)
            {
                float by = key.Key == Key.RightBracket ? SpeedStep : 1.0f / SpeedStep;
                foreach (var (_, dial) in context.Query<SpinDial>())
                {
                    dial.Speed = Math.Clamp(dial.Speed * by, SpinDial.MinSpeed, SpinDial.MaxSpeed);
                    context.Log.Info($"{(by > 1 ? "]" : "[")}: the spin's speed is x{dial.Speed:0.00}.");
                }
            }
            else if (key.Key == Key.Space)
            {
                // Held until this system returns, then delivered to the C# Spawner and to C++ alike.
                context.Events.Send(new Burst { Count = 5 });
            }
        }
    }
}
