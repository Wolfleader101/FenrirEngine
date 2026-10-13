using Fenrir.Scripting;

namespace Fenrir.ScriptFixture;

// Eight small components, to prove the widest query.
public struct Q1 : IComponent { public int V; }
public struct Q2 : IComponent { public int V; }
public struct Q3 : IComponent { public int V; }
public struct Q4 : IComponent { public int V; }
public struct Q5 : IComponent { public int V; }
public struct Q6 : IComponent { public int V; }
public struct Q7 : IComponent { public int V; }
public struct Q8 : IComponent { public int V; }

// Queries from a script, called by the tests through Iris.
public static class Queries
{
    public static float SumVelocityX()
    {
        float sum = 0;
        foreach (var (entity, velocity) in World.Query<Velocity>())
            sum += velocity.Linear.X;
        return sum;
    }

    // Native Transform and script Velocity together: moves by the velocity, writing the engine's own memory.
    public static int Integrate()
    {
        int visited = 0;
        foreach (var (entity, transform, velocity) in World.Query<Transform, Velocity>())
        {
            transform.Position += velocity.Linear;
            visited++;
        }
        return visited;
    }

    // The callback form, which mirrors native ForEach.
    public static int CountUp()
    {
        int visited = 0;
        World.Query<Counter>().ForEach((Entity entity, Ref<Counter> counter) =>
        {
            counter.Hits += 1;
            visited++;
        });
        return visited;
    }

    public static int VisitEmpty() => Visit<Tagged>();

    private static int Visit<T>() where T : unmanaged, IComponent
    {
        int visited = 0;
        foreach (var row in World.Query<T>())
            visited++;
        return visited;
    }

    // The entity in each row is the one that owns the components.
    public static bool RowsAgree()
    {
        foreach (var (entity, velocity, counter) in World.Query<Velocity, Counter>())
        {
            if (entity.Get<Velocity>().Linear.X != velocity.Linear.X || entity.Get<Counter>().Hits != counter.Hits)
                return false;
        }
        return true;
    }

    // Every structural change there is, while iterating. The iteration must not notice any of it.
    public static int ChangeWhileIterating()
    {
        int visited = 0;
        foreach (var (entity, velocity) in World.Query<Velocity>())
        {
            visited++;
            entity.Remove<Velocity>();
            entity.Add<Counter>();
            Entity.Create().Add(new Velocity { Linear = new Vector3(100, 0, 0) });
            if (visited == 1)
                entity.Destroy();
        }
        return visited;
    }

    // The callback form, changing things inside it too.
    public static int ChangeInsideForEach()
    {
        int visited = 0;
        World.Query<Velocity>().ForEach((Entity entity, Ref<Velocity> velocity) =>
        {
            visited++;
            entity.Remove<Velocity>();
            Entity.Create().Add<Velocity>();
        });
        return visited;
    }

    public static int Nested()
    {
        int pairs = 0;
        foreach (var (outer, velocity) in World.Query<Velocity>())
            foreach (var (inner, counter) in World.Query<Counter>())
                pairs++;
        return pairs;
    }

    // All eight at once.
    public static int AllEight()
    {
        int sum = 0;
        foreach (var (entity, a, b, c, d, e, f, g, h) in World.Query<Q1, Q2, Q3, Q4, Q5, Q6, Q7, Q8>())
            sum += a.V + b.V + c.V + d.V + e.V + f.V + g.V + h.V;
        return sum;
    }

    // Leaving the loop early still gives the rows back (no leak, no crash).
    public static int StopEarly()
    {
        int visited = 0;
        foreach (var (entity, velocity) in World.Query<Velocity>())
        {
            visited++;
            if (visited == 2)
                break;
        }
        return visited;
    }

    public static string QueryUnregistered() => Message(() =>
    {
        foreach (var row in World.Query<Unregistered>()) { }
    });

    public static string QueryTwice() => Message(() =>
    {
        foreach (var row in World.Query<Velocity, Velocity>()) { }
    });

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
