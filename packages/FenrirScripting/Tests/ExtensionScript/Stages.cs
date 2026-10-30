using Fenrir.Scripting;
using Fenrir.TestExtension;

namespace Fenrir.TestExtensionScript;

// Stage systems that join the test extension's bench. Each traces itself through the extension, into the trace its C++
// items add to, so a test reads the order everything ran in. They run only when a test runs the bench.

[StageSystem(Bench.Stage, Order = 15)]
public sealed class BenchMiddle : ISystem
{
    public void OnUpdate(in SystemContext context) => Gadgets.Trace("C# 15");
}

// Two at one order, which run by name, after the bench's C++ item at that order.
[StageSystem(Bench.Stage, Order = 20)]
public sealed class BenchTieB : ISystem
{
    public void OnUpdate(in SystemContext context) => Gadgets.Trace("C# 20 B");
}

[StageSystem(Bench.Stage, Order = 20)]
public sealed class BenchTieA : ISystem
{
    public void OnUpdate(in SystemContext context) => Gadgets.Trace("C# 20 A");
}

// Makes an entity with a Charge of 7 each run: alive at once, given its Charge only when the bench returns.
[StageSystem(Bench.Stage, Order = 30)]
public sealed class BenchSpawner : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        Entity entity = context.CreateEntity();
        entity.Add(new Charge { Level = 7 });
        Gadgets.Trace("C# 30 spawned " + entity.Value);
    }
}

// Runs after the spawner at its order. Once armed, it makes an entity with a Charge of 99 and throws: what it made is
// dropped, the spawner's is kept, and it is disabled until the next reload (which also disarms it).
[StageSystem(Bench.Stage, Order = 30)]
public sealed class BenchThrower : ISystem
{
    public static bool Armed;

    public void OnUpdate(in SystemContext context)
    {
        Gadgets.Trace("C# 30 thrower");
        if (!Armed)
            return;
        Entity made = context.CreateEntity();
        made.Add(new Charge { Level = 99 });
        Gadgets.Trace("C# 30 dropped " + made.Value);
        throw new InvalidOperationException("bench kaboom");
    }
}

public static class StageCalls
{
    public static void ArmThrower() => BenchThrower.Armed = true;
}
