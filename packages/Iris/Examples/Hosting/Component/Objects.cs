using System;

namespace Iris.Examples.Component;

// Instance types for checking construction, overloads, virtual dispatch and ownership.
public class Counter
{
    protected int value;

    public Counter() { }
    public Counter(int start) { value = start; }
    public Counter(int start, double scale) { value = (int)(start * scale); }
    public Counter(bool failToConstruct)
    {
        if (failToConstruct) throw new InvalidOperationException("Constructor failure — π \U0001F43A.");
    }

    public int Value() => value;
    public void Add(int amount) => value += amount;
    public virtual int Next() => ++value;
    public int Fail() => throw new InvalidOperationException("Instance method failure.");
}

public sealed class StepCounter : Counter
{
    public StepCounter() { }
    public StepCounter(int start) : base(start) { }

    // Called through a Counter handle, this override must run.
    public override int Next()
    {
        value += 10;
        return value;
    }
}

public abstract class Shape
{
    public abstract int Sides();
}

public class Other
{
    public int Ping() => 7;
}
