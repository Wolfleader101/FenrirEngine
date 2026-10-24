using Fenrir.Scripting;

namespace Fenrir.ScriptFixture;

// Systems the engine runs. They do nothing until a test calls SystemProbe.Enable, so the tests that load this fixture for
// other reasons are not disturbed by them. What they did is recorded in SystemProbe.Events.
public static class SystemProbe
{
    public static bool Enabled;

    public static readonly List<string> Events = new();

    public static void Enable() => Enabled = true;

    public static void Add(string text)
    {
        if (Enabled)
            Events.Add(text);
    }

    public static string Dump() => string.Join(",", Events);

    public static int VelocityEntities(in SystemContext context)
    {
        int count = 0;
        foreach (var (entity, velocity) in context.Query<Velocity>())
            count++;
        return count;
    }
}

[System(Phase.PreUpdate)]
public sealed class PreSystem : ISystem
{
    public void OnUpdate(in SystemContext context) => SystemProbe.Add("Pre");
}

[System(Phase.Tick)]
public sealed class TickSystem : ISystem
{
    public void OnUpdate(in SystemContext context) => SystemProbe.Add("Tick");
}

// Order 5: makes an entity every frame. The change is played back as this system returns.
[System(Phase.Update, Order = 5)]
public sealed class CreatorSystem : ISystem
{
    public void OnCreate(in SystemContext context) => context.Log.Info("Creator.OnCreate");

    public void OnUpdate(in SystemContext context)
    {
        if (!SystemProbe.Enabled)
            return;
        context.CreateEntity().Add<Velocity>();
    }

    public void OnDestroy(in SystemContext context) => context.Log.Info("Creator.OnDestroy");
}

// Order 10: sees the entity the previous system made, so changes are played back after each system, not after the phase.
[System(Phase.Update, Order = 10)]
public sealed class EarlySystem : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        if (SystemProbe.Enabled)
            SystemProbe.Add("Early=" + SystemProbe.VelocityEntities(context));
    }
}

// Order 15: makes an entity and then throws. Its changes are dropped, it is logged once, and it never runs again.
[System(Phase.Update, Order = 15)]
public sealed class ThrowerSystem : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        if (!SystemProbe.Enabled)
            return;
        context.CreateEntity().Add<Velocity>();
        throw new InvalidOperationException("kaboom");
    }
}

[System(Phase.Update, Order = 20)]
public sealed class LateSystem : ISystem
{
    public void OnUpdate(in SystemContext context) => SystemProbe.Add("Late");
}

[System(Phase.Update, Order = 30)]
public sealed class ObserverSystem : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        if (SystemProbe.Enabled)
            SystemProbe.Add("Seen=" + SystemProbe.VelocityEntities(context));
    }
}

// Order 1 but the last name alphabetically: it runs first, so Order is what decides, not the name.
[System(Phase.Update, Order = 1)]
public sealed class ZLeadSystem : ISystem
{
    public void OnUpdate(in SystemContext context) => SystemProbe.Add("Lead");
}

// The same Order: the tie is broken by name, so the result is the same on every run.
[System(Phase.Update, Order = 12)]
public sealed class TieB : ISystem
{
    public void OnUpdate(in SystemContext context) => SystemProbe.Add("TieB");
}

[System(Phase.Update, Order = 12)]
public sealed class TieA : ISystem
{
    public void OnUpdate(in SystemContext context) => SystemProbe.Add("TieA");
}

[System(Phase.PostUpdate)]
public sealed class PostSystem : ISystem
{
    public void OnUpdate(in SystemContext context) => SystemProbe.Add("Post");
}
