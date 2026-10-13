using Fenrir.Scripting;

namespace DemoScripts;

// EVENTS THAT COME FROM C++, AND EVENTS THAT GO TO IT
//
// The window sends a KeyboardKeyEvent from C++ for every key press. The app registered it as "Demo.KeyboardKey" and this is its C#
// mirror; the enum matches the C++ InputState. Burst is the other way round: C# sends it, and both C# (Sparks.cs) and C++
// (ScriptingDemo.hpp, LogBursts) read it. In both cases there is one queue, with the engine's usual two-frame window.

public enum InputState : int
{
    Released = 0,
    Pressed = 1,
    Held = 2,
}

[NativeEvent("Demo.KeyboardKey")]
public struct KeyboardKey : IEvent
{
    public int Key;
    public int Scancode;
    public int Repeat;
    public InputState State;
    public int Mods;
}

[NativeEvent("Demo.Burst")]
public struct Burst : IEvent
{
    public int Count;
}

// An event stays readable for two frames, so a system that reads every frame sees each event twice. This remembers how many of
// what it can see were sent this frame, so a system can handle each event once.
public struct FreshEvents
{
    private int lastFrame;

    /// <summary>How many of the last <paramref name="visible"/> events were sent this frame.</summary>
    public int Next(int visible)
    {
        int fresh = Math.Max(0, visible - lastFrame);
        lastFrame = fresh;
        return fresh;
    }
}

public static class Keys
{
    public const int Space = 32;
    public const int R = 82;
}

// R reverses every spin. Space starts a burst of sparks.
[System(Phase.Update, Order = 5)]
public sealed class Controls : ISystem
{
    private FreshEvents fresh;

    public void OnUpdate(in SystemContext context)
    {
        var keys = context.Events.Read<KeyboardKey>();
        int first = keys.Count - fresh.Next(keys.Count);
        for (int index = first; index < keys.Count; index++)
        {
            KeyboardKey key = keys[index];
            if (key.State != InputState.Pressed)
                continue;

            if (key.Key == Keys.R)
            {
                foreach (var (entity, spin) in context.Query<Spin>())
                    spin.RadiansPerSecond = -spin.RadiansPerSecond;
                context.Log.Info("R: reversed the spin.");
            }
            else if (key.Key == Keys.Space)
            {
                // Held until this system returns, then delivered to the C# Spawner and to C++ alike.
                context.Events.Send(new Burst { Count = 5 });
            }
        }
    }
}
