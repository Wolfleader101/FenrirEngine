using Fenrir.Scripting;

namespace Fenrir.Physics;

// What PhysicsWorld's queries and state functions take and give. Each is laid out exactly as FenrirPhysicsScripting's
// native mirror of the C++ type, with an entity as its 32-bit value; the native side checks its own sizes at compile time,
// and the scripting tests read every field back.

/// <summary>Which bodies a query can find. New, it finds every body except triggers.</summary>
public struct QueryFilter
{
    /// <summary>The collision layers to find, one bit per layer: a body is found if it is in any of them.</summary>
    public uint Mask = uint.MaxValue;
    /// <summary>A body never found, such as the one casting the query. None when new.</summary>
    public Entity IgnoreBody = new(uint.MaxValue);
    /// <summary>Whether triggers are found.</summary>
    public bool HitTriggers;

    public QueryFilter()
    {
    }
}

/// <summary>Where a ray hit a body.</summary>
public struct RaycastHit
{
    /// <summary>The entity of the body hit.</summary>
    public Entity Body;
    /// <summary>The entity of the collider hit.</summary>
    public Entity Collider;
    /// <summary>Where the ray met the surface, in world space.</summary>
    public Vector3 Point;
    /// <summary>The surface's normal at <see cref="Point"/>, pointing out of the body.</summary>
    public Vector3 Normal;
    /// <summary>How far along the ray <see cref="Point"/> is: 0 for a ray that starts inside a body.</summary>
    public float Distance;
}

/// <summary>Where a cast shape hit a body.</summary>
public struct ShapeCastHit
{
    /// <summary>The entity of the body hit.</summary>
    public Entity Body;
    /// <summary>The entity of the collider hit.</summary>
    public Entity Collider;
    /// <summary>Where the cast shape touches the body, on the cast shape, in world space.</summary>
    public Vector3 Point;
    /// <summary>The body's surface normal where it was hit, pointing towards the cast shape.</summary>
    public Vector3 Normal;
    /// <summary>How far the shape travelled before it touched: 0 for a shape that starts touching.</summary>
    public float Distance;
}

/// <summary>A body a shape or point overlaps.</summary>
public struct OverlapHit
{
    /// <summary>The entity of the body.</summary>
    public Entity Body;
    /// <summary>The entity of the collider.</summary>
    public Entity Collider;
}

/// <summary>One contact of a body, as of the last step.</summary>
public struct Contact
{
    /// <summary>The body asked about, and its collider touching.</summary>
    public Entity Body;
    public Entity Collider;
    /// <summary>The body it touches, and that body's collider.</summary>
    public Entity OtherBody;
    public Entity OtherCollider;
    /// <summary>Where they touch, in world space.</summary>
    public Vector3 Point;
    /// <summary>World space, unit length, pointing from <see cref="Body"/> to <see cref="OtherBody"/>.</summary>
    public Vector3 Normal;
}

/// <summary>An axis-aligned box in world space.</summary>
public struct Aabb
{
    public Vector3 Min;
    public Vector3 Max;
}

/// <summary>Which categories of debug lines a physics world draws. Bit flags.</summary>
[Flags]
public enum DebugDraw : uint
{
    None = 0,
    /// <summary>Each collider's wireframe at its body's pose, coloured by motion type.</summary>
    Colliders = 1u << 0,
    /// <summary>Each contact between solid bodies.</summary>
    Contacts = 1u << 1,
    /// <summary>A line from each moving body's position along its linear velocity.</summary>
    Velocities = 1u << 2,
    /// <summary>Each body's world-space bounding box.</summary>
    Bounds = 1u << 3,
    All = Colliders | Contacts | Velocities | Bounds,
}
