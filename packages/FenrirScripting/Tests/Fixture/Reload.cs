using Fenrir.Scripting;

namespace Fenrir.ScriptFixture;

// The fixture is built twice: this is "version 1", and a build with VARIANT2 defined is "version 2". The reload tests run
// the first, replace its file with the second, and look at what changed and what carried over.

// Unchanged between the versions: its values must survive a reload.
public struct Stamped : IComponent
{
    public int Amount;
}

// Unchanged between the versions: its queue, and the events in it, must survive.
public struct Echo : IEvent
{
    public int V;
}

// Changes layout in version 2: its queue is dropped.
public struct Ripple : IEvent
{
    public int V;
#if VARIANT2
    public int W;
#endif
}

public static class ReloadProbe
{
    public static int Version
    {
        get
        {
#if VARIANT2
            return 2;
#else
            return 1;
#endif
        }
    }

    public static int Answer() => Version;

    public static void SendEcho(int value) => default(SystemContext).Events.Send(new Echo { V = value });

    public static void SendRipple(int value) => default(SystemContext).Events.Send(new Ripple { V = value });
}

// Order 60: adds the version to every Stamped each frame, so a reload shows as a change in the step.
[System(Phase.Update, Order = 60)]
public sealed class StampSystem : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        foreach (var (entity, stamped) in context.Query<Stamped>())
            stamped.Amount += ReloadProbe.Version;
    }
}
