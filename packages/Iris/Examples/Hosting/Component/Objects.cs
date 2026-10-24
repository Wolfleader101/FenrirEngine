using System;

namespace Iris.Examples.Component;

// Instance types for checking construction, overloads, virtual dispatch and ownership.
public class Counter
{
    protected int value;

    public Counter() { }
    public Counter(int start) { value = start; }
    public Counter(int start, double scale) { value = (int)(start * scale); }
    public Counter(Counter copy) { value = copy.value; }
    public Counter(bool failToConstruct)
    {
        if (failToConstruct) throw new InvalidOperationException("Constructor failure — π \U0001F43A.");
    }

    public int Value() => value;
    public void Add(int amount) => value += amount;
    public virtual int Next() => ++value;
    public int Fail() => throw new InvalidOperationException("Instance method failure.");

    // Objects as arguments and returns.
    public Counter Clone() => new Counter(value);       // A new object every time.
    public Counter Self() => this;                      // The same object every time.
    public Counter Nothing() => null!;                  // A null return.
    public int Combine(Counter other) => value + other.value;
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

// Hides Counter.Value with `new`: reflection sees both, and the most-derived one must win.
public class HidingCounter : Counter
{
    public new int Value() => 42;
}

public abstract class Shape
{
    public abstract int Sides();
}

public class Other
{
    public int Ping() => 7;
}

// Static members taking and returning objects. Describe is overloaded on reference types only, so a
// signature that cannot name the managed type is ambiguous for it.
public static class Factory
{
    public static Counter Make(int start) => new Counter(start);
    public static Counter Nothing() => null!;
    public static int Total(Counter first, Counter second) => first.Value() + second.Value();
    public static int Describe(Counter counter) => 1;
    public static int Describe(Other other) => 2;
}
