using Fenrir.Scripting;

namespace Fenrir.ScriptFixture;

// Structural changes from a script. The tests call these through Iris, then play the changes back as the runner will.
public static class Changes
{
    // A new entity, used at once.
    public static uint CreateWithVelocity()
    {
        var entity = Entity.Create();
        entity.Add(new Velocity { Linear = new Vector3(1, 2, 3) });
        entity.Add<Counter>();
        return entity.Value;
    }

    public static uint CreateAndDestroy()
    {
        var entity = Entity.Create();
        entity.Add<Velocity>();
        entity.Destroy();
        return entity.Value;
    }

    // Takes a Ref, then changes the pool it points into every way there is. The Ref must still be good at the end.
    public static float RefSurvivesChanges(uint kept, uint victim)
    {
        var keptVelocity = new Entity(kept).Get<Velocity>();
        keptVelocity.Linear = new Vector3(5, 6, 7);

        new Entity(victim).Remove<Velocity>();
        var created = Entity.Create();
        for (int index = 0; index < 5000; index++)
            Entity.Create().Add<Velocity>(); // Far more than a page of the pool.
        created.Add<Velocity>();
        // A write after all of that must land in the kept entity's own element (and move with it at playback).
        keptVelocity.Linear.X = 9;
        return keptVelocity.Linear.X + keptVelocity.Linear.Y + keptVelocity.Linear.Z;
    }

    // Reads see the state now, not the state after the changes.
    public static bool AddThenHas(uint entity)
    {
        new Entity(entity).Add<Velocity>();
        return new Entity(entity).Has<Velocity>();
    }

    public static bool RemoveThenHas(uint entity)
    {
        new Entity(entity).Remove<Velocity>();
        return new Entity(entity).Has<Velocity>();
    }

    public static bool DestroyThenIsValid(uint entity)
    {
        new Entity(entity).Destroy();
        return new Entity(entity).IsValid && new Entity(entity).Has<Transform>();
    }

    public static void DestroyThenAdd(uint entity)
    {
        var e = new Entity(entity);
        e.Destroy();
        e.Add<Velocity>();
    }

    public static void AddThenDestroy(uint entity)
    {
        var e = new Entity(entity);
        e.Add<Velocity>();
        e.Destroy();
    }

    public static void DestroyTwice(uint entity)
    {
        new Entity(entity).Destroy();
        new Entity(entity).Destroy();
    }

    // Order matters: add A, replace with B, take it off, add C. C is what is left.
    public static void ChangesApplyInOrder(uint entity)
    {
        var e = new Entity(entity);
        e.Add(new Velocity { Linear = new Vector3(1, 0, 0) });
        e.Add(new Velocity { Linear = new Vector3(2, 0, 0) });
        e.Remove<Velocity>();
        e.Add(new Velocity { Linear = new Vector3(3, 0, 0) });
    }

    // Remove then add: the component comes back, as a default one.
    public static void RemoveThenAdd(uint entity)
    {
        var e = new Entity(entity);
        e.Remove<Velocity>();
        e.Add<Velocity>();
    }

    public static void SetTransform(uint entity)
    {
        new Entity(entity).Add(new Transform
        {
            Position = new Vector3(9, 8, 7),
            Rotation = new Quaternion { W = 1 },
            Scale = new Vector3(2, 2, 2),
        });
    }

    // What an exception says. Each returns the message, or "no exception".
    public static string TryGetMissing(uint entity) => Message(() => new Entity(entity).Get<Velocity>());

    public static string TryAddOnStale(uint entity) => Message(() => new Entity(entity).Add<Velocity>());

    public static string TryDestroyStale(uint entity) => Message(() => new Entity(entity).Destroy());

    public static string TryRemoveRequired(uint entity) => Message(() => new Entity(entity).Remove<Transform>());

    public static string TryRemoveName(uint entity) => Message(() => new Entity(entity).Remove<Name>());

    private static string Message(Action action)
    {
        try
        {
            action();
            return "no exception";
        }
        catch (InvalidOperationException exception)
        {
            return exception.Message;
        }
    }
}
