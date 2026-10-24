using System;

namespace Iris.Examples.Component;

// Members for the member queries: a struct with offsets and a field attribute, and a class with a base class,
// properties, methods, a hidden backing field and an indexer that must not be listed.
public struct Probe
{
    [Script("opacity", 2, Weight = 0.5)]
    public float Opacity;
    public Vec3 Position;
    public bool Active;
    private int hidden;
    public static int Count;
    public const int Max = 4;
    public Mode Style;

    public int Hidden => hidden;
}

public class ProbeBase
{
    public int Inherited;
    public void InheritedMethod() { }
}

public class Probed : ProbeBase
{
    [Obsolete("old")]
    public int Legacy;
    protected internal long Wide;
    public readonly string Fixed = "fixed";
    public static double Shared;

    public string? Title { get; set; }
    [Script("size", Tag = "range")]
    public int Size { get; private set; }
    public int Computed => 3;
    public static bool Flag { get; set; }
    public int this[int index] => index;

    [Script("scale")]
    public float Scale(float factor, string label) => factor;
    public static void Reset() { }
    private int Secret(Probe probe, int[] values, Probed other) => 0;
}
