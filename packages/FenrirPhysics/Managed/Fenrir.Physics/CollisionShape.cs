using System.Runtime.InteropServices;
using Fenrir.Scripting;

namespace Fenrir.Physics;

/// <summary>Which kind of shape a <see cref="CollisionShape"/> holds.</summary>
public enum ShapeType : byte
{
    Box,
    Sphere,
    Capsule,
    ConvexHull,
    Mesh,
}

/// <summary>A box centred on its entity, <see cref="HalfExtents"/> from the centre to each face before scale.</summary>
public struct BoxShape
{
    public Vector3 HalfExtents = new(0.5f, 0.5f, 0.5f);

    public BoxShape()
    {
    }
}

/// <summary>A sphere centred on its entity.</summary>
public struct SphereShape
{
    public float Radius = 0.5f;

    public SphereShape()
    {
    }
}

/// <summary>A capsule centred on its entity and standing along its local y axis.</summary>
public struct CapsuleShape
{
    public float HalfHeight = 0.5f;
    public float Radius = 0.5f;

    public CapsuleShape()
    {
    }
}

/// <summary>
/// A handle to a collision mesh made by <see cref="PhysicsWorld.CreateCollisionMesh"/>. The default handle refers to no mesh.
/// </summary>
public struct CollisionMesh : IEquatable<CollisionMesh>
{
    public ulong Id;

    public readonly bool IsNone => Id == 0;

    public readonly bool Equals(CollisionMesh other) => Id == other.Id;
    public override readonly bool Equals(object? obj) => obj is CollisionMesh other && Equals(other);
    public override readonly int GetHashCode() => Id.GetHashCode();
    public static bool operator ==(CollisionMesh left, CollisionMesh right) => left.Equals(right);
    public static bool operator !=(CollisionMesh left, CollisionMesh right) => !left.Equals(right);
}

/// <summary>The convex hull of a collision mesh's vertices.</summary>
public struct ConvexHullShape
{
    public CollisionMesh Hull;
}

/// <summary>A collision mesh's triangles. Prefer it for static level geometry, and a hull for anything that moves.</summary>
public struct MeshShape
{
    public CollisionMesh Mesh;
}

/// <summary>
/// A collision shape: exactly one kind of shape and its dimensions, by value, laid out as the native
/// <c>Fenrir::CollisionShape</c> (a one-byte tag, then the kinds overlapping at the union's offset, 8 because a
/// <see cref="CollisionMesh"/> holds a 64-bit id). Assigning a kind changes it; each kind's view reads or edits it in place
/// and throws <see cref="InvalidOperationException"/> if the shape is another kind. New, it is a unit box.
/// </summary>
[TaggedUnion(Noun = "shape")]
[StructLayout(LayoutKind.Explicit)]
public partial struct CollisionShape
{
    [FieldOffset(0)] private ShapeType _type;
    [FieldOffset(8)] private BoxShape _box;
    [FieldOffset(8)] private SphereShape _sphere;
    [FieldOffset(8)] private CapsuleShape _capsule;
    [FieldOffset(8)] private ConvexHullShape _convexHull;
    [FieldOffset(8)] private MeshShape _mesh;
}
