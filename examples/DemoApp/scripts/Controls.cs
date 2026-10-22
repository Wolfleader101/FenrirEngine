using Fenrir.Math;
using Fenrir.Scripting;

namespace DemoScripts;

// EVENTS THAT COME FROM C++, AND EVENTS THAT GO TO IT
//
// The window sends a KeyboardKeyEvent from C++ for every key press. The app registered it as "Demo.KeyboardKey" and this is its C#
// mirror; the enum matches the C++ InputState. Burst is the other way round: C# sends it, and both C# (Sparks.cs) and C++
// (ScriptingDemo.hpp, LogBursts) read it. In both cases there is one queue, and each reader, in either language, sees each event
// once.

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

// A left click in the editor's scene view, as the ray from the camera through the clicked point. The editor sends it from
// C++ (SceneClickEvent); the C# physics scene raycasts along it (Physics/Playground.cs).
[NativeEvent("Demo.SceneClick")]
public struct SceneClick : IEvent
{
    public Vector3 Origin;
    public Vector3 Direction; // Unit length.
}

[NativeEvent("Demo.Burst")]
public struct Burst : IEvent
{
    public int Count;
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
    // Remembers which key events this system has already handled, so each is handled once.
    private readonly EventReader<KeyboardKey> keys = new();

    public void OnUpdate(in SystemContext context)
    {
        foreach (var key in keys.Read())
        {
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
