using System.Runtime.Intrinsics;
using Fenrir.Math;
using Fenrir.Scripting;

namespace Fenrir.ScriptFixture;

// Components the script defines itself: no C++ type, no [NativeComponent]. The SDK's generator reports each to the engine
// as the assembly loads, and the engine stores them in EnTT beside the native ones.
public struct Velocity : IComponent
{
    public Vector3 Linear;
}

public struct Counter : IComponent
{
    public int Hits;
    public long Total;
#if VARIANT2
    public int Extra; // Version 2 of the fixture changes this component's layout.
#endif
}

// 16-byte aligned: a Vector128 must start on a 16-byte boundary, and the engine's storage has to honour that.
public struct Aligned : IComponent
{
    public Vector128<float> Lanes;
    public byte Mark;
}

// No data at all: a tag.
public struct Tagged : IComponent
{
}

public static class Scripted
{
    public static void Accelerate(uint entity)
    {
        var v = new Entity(entity).Get<Velocity>();
        v.Linear += new Vector3(1, 2, 3);
    }

    public static float SpeedX(uint entity) => new Entity(entity).Get<Velocity>().Linear.X;

    public static long Count(uint entity)
    {
        var counter = new Entity(entity).Get<Counter>();
        counter.Hits += 1;
        counter.Total += 10;
        return counter.Total;
    }

    public static void FillAligned(uint entity)
    {
        var a = new Entity(entity).Get<Aligned>();
        a.Lanes = Vector128.Create(1f, 2f, 3f, 4f);
        a.Mark = 7;
    }

    public static float AlignedLane(uint entity, int lane) => new Entity(entity).Get<Aligned>().Lanes.GetElement(lane);

    public static bool HasVelocity(uint entity) => new Entity(entity).Has<Velocity>();

    public static bool HasTagged(uint entity) => new Entity(entity).Has<Tagged>();
}
