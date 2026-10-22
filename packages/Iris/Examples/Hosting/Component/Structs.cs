using System.Runtime.InteropServices;

namespace Iris.Examples.Component;

// Blittable structs: plain data the native side can mirror byte for byte.
public struct Vec3
{
    public float X, Y, Z;
}

public struct Pose
{
    public Vec3 Position;
    public Vec3 Scale;
    public double Weight;
    public byte Flags;
}

// A bool is one byte in memory, as in C++, so a struct holding one is plain data too.
public struct WithBool
{
    public bool Flag;
    public int Value;
}

// Not blittable: a reference has no native representation.
public struct WithText
{
    public string Name;
}

// Explicit layout is fine when it matches.
[StructLayout(LayoutKind.Explicit)]
public struct Packed
{
    [FieldOffset(0)] public int Low;
    [FieldOffset(4)] public int High;
}

// An opaque pointer: native code keeps an address in Owner, and managed code only carries it.
public struct Linked
{
    public uint Value;
    public uint Reserved;
    public ulong Owner;
}

// Wrong on purpose for a native pointer: the same size, but only four bytes where the pointer is.
[StructLayout(LayoutKind.Explicit, Size = 16)]
public struct NarrowLinked
{
    [FieldOffset(0)] public uint Value;
    [FieldOffset(8)] public uint Owner;
}

// An empty struct: one byte, as in C++, and no fields.
public struct Empty
{
}

public static class Structs
{
    public static Vec3 Add(Vec3 left, Vec3 right) => new() { X = left.X + right.X, Y = left.Y + right.Y, Z = left.Z + right.Z };
    public static float Dot(Vec3 left, Vec3 right) => left.X * right.X + left.Y * right.Y + left.Z * right.Z;
    public static Vec3 Scale(Vec3 value, float factor) => new() { X = value.X * factor, Y = value.Y * factor, Z = value.Z * factor };
    public static Pose Move(Pose pose, Vec3 delta)
    {
        pose.Position = Add(pose.Position, delta);
        pose.Flags = (byte)(pose.Flags + 1);
        return pose;
    }
    public static Packed Swap(Packed value) => new() { Low = value.High, High = value.Low };
    public static WithBool Flip(WithBool value) => new() { Flag = !value.Flag, Value = value.Value };
    public static int Length(WithText value) => value.Name?.Length ?? 0;
    public static int Mixed(int first, Vec3 second, double third) => first + (int)second.X + (int)third;
    public static Linked Bump(Linked value) => new() { Value = value.Value + 1, Reserved = value.Reserved, Owner = value.Owner };
    public static ulong OwnerOf(Linked value) => value.Owner;
    public static Linked Adopt(ulong owner) => new() { Owner = owner };
    public static Empty Same(Empty value) => value;
}

public class Body
{
    public Vec3 Position;
    public static Vec3 Origin;
    public Vec3 Velocity { get; set; }
    public Pose Pose;
    public Vec3 Moved(Vec3 by) => new() { X = Position.X + by.X, Y = Position.Y + by.Y, Z = Position.Z + by.Z };
    public Body() { }
    public Body(Vec3 position) => Position = position;
}
