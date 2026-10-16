using System.Diagnostics.CodeAnalysis;
using System.Runtime.InteropServices;
using Fenrir.Scripting;

namespace Fenrir.ScriptFixture;

// The C# mirror of a native component holding a tagged union (the test's TestNative::Collider), written by hand in the
// pattern FenrirScripting's README describes: a private tag, each kind at the union's offset, a checked view per kind and
// implicit conversions from each kind.
public enum ShapeType
{
    Box,
    Sphere,
}

public struct BoxShape
{
    public Vec3 HalfExtents;
}

public struct SphereShape
{
    public float Radius;
}

[StructLayout(LayoutKind.Explicit)]
public struct Shape
{
    [FieldOffset(0)] private ShapeType _type;
    [FieldOffset(4)] private BoxShape _box;
    [FieldOffset(4)] private SphereShape _sphere;

    public readonly ShapeType Type => _type;

    // A view of the shape as a box, which it must be. It is a reference into the shape, so writes through it land in the
    // component's storage.
    [UnscopedRef]
    public ref BoxShape Box
    {
        get
        {
            Expect(ShapeType.Box);
            return ref _box;
        }
    }

    [UnscopedRef]
    public ref SphereShape Sphere
    {
        get
        {
            Expect(ShapeType.Sphere);
            return ref _sphere;
        }
    }

    public static implicit operator Shape(BoxShape box) => new() { _type = ShapeType.Box, _box = box };

    public static implicit operator Shape(SphereShape sphere) => new() { _type = ShapeType.Sphere, _sphere = sphere };

    private readonly void Expect(ShapeType wanted)
    {
        if (_type != wanted)
            throw new InvalidOperationException($"The shape is a {_type}, not a {wanted}.");
    }
}

[NativeComponent("Test.Collider")]
public struct Collider : IComponent
{
    public Shape Shape;
}

// Deliberately wrong mirrors of the same native layout, for the tests of what the load refuses. Only those tests register
// them. The fields are public so the compiler does not warn that nothing reads them.
[StructLayout(LayoutKind.Explicit)]
public struct MisplacedShape
{
    [FieldOffset(0)] public ShapeType _type;
    [FieldOffset(4)] public BoxShape _box;
    [FieldOffset(8)] public SphereShape _sphere; // the union is at 4
}

[NativeComponent("Test.MisplacedCollider")]
public struct MisplacedCollider : IComponent
{
    public MisplacedShape Shape;
}

[StructLayout(LayoutKind.Explicit)]
public struct MisnamedShape
{
    [FieldOffset(0)] public ShapeType _type;
    [FieldOffset(4)] public BoxShape _box;
    [FieldOffset(4)] public SphereShape _ball; // the native side calls it _sphere
}

[NativeComponent("Test.MisnamedCollider")]
public struct MisnamedCollider : IComponent
{
    public MisnamedShape Shape;
}

// What ShapeSystem saw, for the tests. The system does nothing until a test calls Enable (Test.Collider is registered only
// by the tagged-union tests).
public static class ShapeProbe
{
    public static bool Enabled;

    private static float seenRadius = -1;

    public static void Enable() => Enabled = true;

    public static float SeenRadius() => seenRadius;

    internal static void Saw(float radius) => seenRadius = radius;

    // Reads a sphere's box view: the InvalidOperationException's message, or null if it did not throw.
    public static string? WrongKindMessage()
    {
        Shape shape = new SphereShape { Radius = 1 };
        try
        {
            _ = shape.Box.HalfExtents.X;
            return null;
        }
        catch (InvalidOperationException exception)
        {
            return exception.Message;
        }
    }
}

// Makes each sphere a box: reads the radius C++ wrote, assigns the new kind by value, then edits it through its view.
[System(Phase.Update)]
public sealed class ShapeSystem : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        if (!ShapeProbe.Enabled)
            return;
        foreach (var (entity, collider) in context.Query<Collider>())
        {
            if (collider.Shape.Type != ShapeType.Sphere)
                continue;
            ShapeProbe.Saw(collider.Shape.Sphere.Radius);
            collider.Shape = new BoxShape { HalfExtents = new Vec3 { X = 1, Y = 1, Z = 1 } };
            collider.Shape.Box.HalfExtents.Y = 2;
        }
    }
}
