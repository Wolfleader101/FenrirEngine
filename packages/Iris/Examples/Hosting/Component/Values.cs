using System.Runtime.CompilerServices;

// Marshalling is off for this component: bool is one byte and char two, wherever they appear in a struct or
// an internal call signature. Iris requires this for an internal call that uses either (it cannot otherwise
// know their unmanaged size), and reads struct bytes directly.
[assembly: DisableRuntimeMarshalling]

namespace Iris.Examples.Component;

// A char is a UTF-16 code unit, two bytes, as char16_t is in C++.
public struct Glyph
{
    public char Letter;
    public bool Bold;
    public ushort Code;
}

// Enums cross as their underlying integer, which the native enum must match exactly.
public enum Gear
{
    Off = 0,
    On = 1,
    Auto = 7
}

public enum Shade : byte
{
    Dark = 1,
    Light = 2
}

public enum Offset : short
{
    Negative = -5,
    Positive = 5
}

public struct Setting
{
    public Gear Current;
    public Shade Tint;
    public bool Locked;
    public int Level;
}

public class Dial
{
    public Gear Selected;
    public static Gear Default = Gear.Auto;
    public Shade Tone { get; set; } = Shade.Dark;
}

// Fixed-size arrays inside a struct: a fixed buffer (C# only allows primitive elements) and [InlineArray]
// structs (any blittable element). The native side mirrors them as `T[N]` or `std::array<T, N>`.
public unsafe struct Label
{
    public int Id;
    public fixed byte Text[16];
    public fixed char Wide[4];
}

[InlineArray(4)]
public struct Weights
{
    private float element;
}

[InlineArray(2)]
public struct Points
{
    private Vec3 element;
}

[InlineArray(3)]
public struct Gears
{
    private Gear element;
}

[InlineArray(3)]
public struct Ready
{
    private bool element;
}

public struct Bundle
{
    public Weights Weights;
    public Points Points;
    public Gears Gears;
    public Ready Ready;
}

// A deliberate leak for the unload checks: a thread running component code keeps the component from being
// collected until it returns. It polls a native flag, so the host decides when it ends.
public static class Leaks
{
    public static void Start()
    {
        var thread = new System.Threading.Thread(() =>
        {
            while (!Natives.CallLeakStopped()) System.Threading.Thread.Sleep(5);
        }) { IsBackground = true };
        thread.Start();
    }
}

// bool, char, enums and inline arrays across the boundary.
public static class Values
{
    public static unsafe Label Rename(Label value)
    {
        value.Id += 1;
        value.Wide[0] = (char)(value.Wide[0] + 1);
        byte[] text = System.Text.Encoding.ASCII.GetBytes("renamed");
        for (int index = 0; index < 16; ++index) value.Text[index] = index < text.Length ? text[index] : (byte)0;
        return value;
    }

    // Every array reversed, so an element in the wrong place shows.
    public static Bundle Reverse(Bundle value)
    {
        Bundle result = default;
        for (int index = 0; index < 4; ++index) result.Weights[index] = value.Weights[3 - index];
        for (int index = 0; index < 2; ++index) result.Points[index] = value.Points[1 - index];
        for (int index = 0; index < 3; ++index)
        {
            result.Gears[index] = value.Gears[2 - index];
            result.Ready[index] = !value.Ready[2 - index];
        }
        return result;
    }

    public static Gear Cycle(Gear value) => value switch { Gear.Off => Gear.On, Gear.On => Gear.Auto, _ => Gear.Off };
    public static Shade Invert(Shade value) => value == Shade.Dark ? Shade.Light : Shade.Dark;
    public static Offset Mirror(Offset value) => (Offset)(-(short)value);
    public static Setting Tune(Setting value) => new() { Current = Cycle(value.Current), Tint = Invert(value.Tint), Locked = !value.Locked, Level = value.Level + 1 };

    // A struct with bool fields returned as it came: any nonzero byte arrives as true, and comes back as 1.
    public static WithBool Same(WithBool value) => value;
    public static bool Both(bool left, bool right) => left && right;

    public static char Next(char value) => (char)(value + 1);
    public static Glyph Bump(Glyph value) => new() { Letter = (char)(value.Letter + 1), Bold = !value.Bold, Code = (ushort)(value.Code + 1) };
    public static char[] Word() => new[] { 'h', 'i', 'π' };
}
