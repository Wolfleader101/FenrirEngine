using Fenrir.Math;
using Fenrir.Scripting;

namespace DemoScripts;

// ENTITIES CREATED AND DESTROYED FROM C#, AND AN EVENT ONLY C# KNOWS
//
// Pressing Space (Controls.cs) sends a Burst. The Spawner turns it into short-lived entities: they show up in the editor's
// hierarchy as "Spark 1", "Spark 2"... and vanish when their time is up. When one goes, the Reaper sends Expired, an event that
// only exists in C#: there is no C++ type and nothing was registered; the build found it and the engine made its queue.

public struct Lifetime : IComponent
{
    public float Remaining; // Seconds left.
    public int Number;      // Which spark this is.
    public int Named;       // Zero until the Dresser has given it a name and a place.
}

// C#-only: nothing in C++ knows this event.
public struct Expired : IEvent
{
    public int Number;
}

// Reads each Burst once and creates that many sparks.
[System(Phase.Update, Order = 30)]
public sealed class Spawner : ISystem
{
    private readonly EventReader<Burst> bursts = new();
    private int next = 1;

    public void OnUpdate(in SystemContext context)
    {
        foreach (var burst in bursts.Read())
        {
            for (int spark = 0; spark < burst.Count; spark++)
            {
                // The entity can be used straight away, but it only exists for the rest of the engine, and has its Transform and
                // Name, when this system returns.
                Entity entity = context.CreateEntity();
                entity.Add(new Lifetime { Remaining = 2.0f + 0.5f * spark, Number = next++ });
            }
        }
    }
}

// Runs after the Spawner, so it sees the sparks the Spawner just made (changes land between systems). Names them and puts them in
// a row above the backpacks, once.
[System(Phase.Update, Order = 31)]
public sealed class Dresser : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        foreach (var row in context.Query<Name, Transform, Lifetime>())
        {
            var lifetime = row.Component3;
            if (lifetime.Named != 0)
                continue;
            lifetime.Named = 1;
            // Name.Text has a setter, so it is set on a local: the variables of a foreach deconstruction cannot be modified.
            var name = row.Component1;
            var transform = row.Component2;
            name.Text = $"Spark {lifetime.Number}";
            transform.Position = new Vector3(-4.0f + 1.0f * (lifetime.Number % 8), 9.0f, 0.0f);
        }
    }
}

// Counts the sparks down, and removes the ones that are done.
[System(Phase.Update, Order = 32)]
public sealed class Reaper : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        float deltaTime = context.Time.DeltaTime;
        foreach (var (entity, lifetime) in context.Query<Lifetime>())
        {
            lifetime.Remaining -= deltaTime;
            if (lifetime.Remaining > 0.0f)
                continue;
            context.Events.Send(new Expired { Number = lifetime.Number });
            entity.Destroy(); // Takes effect when this system returns, so the loop above is never disturbed.
        }
    }
}

// Reads the C#-only event.
[System(Phase.Update, Order = 40)]
public sealed class SparkLog : ISystem
{
    private readonly EventReader<Expired> expired = new();

    public void OnUpdate(in SystemContext context)
    {
        foreach (var spark in expired.Read())
            context.Log.Info($"Spark {spark.Number} burnt out.");
    }
}
