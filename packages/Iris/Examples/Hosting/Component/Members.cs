using System;

namespace Iris.Examples.Component;

// Fields and properties of every shape the member API has to tell apart.
public class Settings
{
    // Static and instance fields.
    public static int StaticCount;
    public static readonly int Limit = 10;
    public const int Constant = 5;
    public int Count;
    public double Ratio;
    public bool Flag;
    public ulong Big;
    public float Single;
    public sbyte Small;
    public readonly int Id = 7;
    public Counter? Child;
    private int secret = 99;

    // Properties.
    public bool Enabled { get; set; }
    public int ReadOnly { get; } = 3;
    public int InitOnly { get; init; } = 11;
    public static int StaticProperty { get; set; }
    public Counter? ChildProperty { get; set; }

    private int written;
    public int WriteOnly { set { written = value; } }
    public int WrittenValue() => written + secret - 99;

    private int checkedValue;
    public int Checked
    {
        get => checkedValue;
        set
        {
            if (value < 0) throw new ArgumentOutOfRangeException(nameof(value), "Negative values are rejected — π.");
            checkedValue = value;
        }
    }

    public int Exploding => throw new InvalidOperationException("Property getter failure.");

    // Indexers are not properties the member API can name.
    public int this[int index] => index;
}

// Hides Settings.Count with a different type: the most-derived declaration is the one a name means.
public class HidingSettings : Settings
{
    public new double Count;
}
