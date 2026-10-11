using System;
using Iris.Examples.Contracts;

namespace Iris.Examples.Component;

public enum Mode { Slow = 1, Fast = 2 }

[AttributeUsage(AttributeTargets.All, AllowMultiple = true)]
public sealed class ScriptAttribute : Attribute
{
    public ScriptAttribute(string name) => Name = name;
    public ScriptAttribute(string name, int priority) : this(name) => Priority = priority;
    public string Name { get; }
    public int Priority { get; }
    public bool Enabled { get; set; }
    public double Weight { get; set; }
    public string? Tag { get; set; }
}

[AttributeUsage(AttributeTargets.All)]
public sealed class DetailsAttribute : Attribute
{
    public DetailsAttribute(Mode mode, Type related, byte small, ulong big, float ratio, char letter, string? missing, int[] list) { }
}

// A small hierarchy for the type queries: abstract base, interface from the contract assembly, an
// interface from the framework, attributes of every constructor argument shape, and types that must not be listed.
[Script("actor", 1, Tag = "base")]
public abstract class Actor : IScript
{
}

[Script("player", 3, Enabled = true, Weight = 0.5)]
[Script("hero")]
[Obsolete("replaced")]
[Details(Mode.Fast, typeof(Actor), 200, ulong.MaxValue, 1.5f, 'x', null, new[] { 1, 2 })]
public class Player : Actor, IDisposable
{
    public void Dispose() { }
}

public class Enemy : Actor
{
}

public sealed class Boss : Enemy
{
}

public struct Marker
{
    public int Value;
}

public static class Plain
{
}

internal class HiddenActor : Actor
{
}

public class Box<T>
{
}
