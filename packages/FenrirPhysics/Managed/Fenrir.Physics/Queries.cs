using Fenrir.Math;
using Fenrir.Scripting;

namespace Fenrir.Physics;

// What PhysicsWorld's queries and state functions take and give. Each is laid out exactly as FenrirPhysicsScripting's
// native mirror of the C++ type, with an entity as its 32-bit value; the native side checks its own sizes at compile time,
// the scripting tests check QueryFilter, MoveCastHit and PenetrationHit against their mirrors' mappings, and they read
// every field back.

/// <summary>Which bodies a query can find. New, it finds every body except triggers.</summary>
public struct QueryFilter
{
    /// <summary>The collision layers to find, one bit per layer: a body is found if it is in any of them.</summary>
    public uint Mask = uint.MaxValue;
    /// <summary>
    /// The layers the query is in, one bit per layer: when non-zero, a body is found only if its CollisionFilter mask
    /// includes one of them, the rule two bodies collide by. 0 when new, which ignores bodies' masks, so a ray still finds
    /// a hitbox that collides with nothing.
    /// </summary>
    /// <remarks>
    /// To find what a body would collide with, filter as it does:
    /// <c>new QueryFilter { Mask = cf.Mask, Layers = cf.Layers, IgnoreBody = me }</c>, with <c>cf</c> its CollisionFilter
    /// (or a new one).
    /// </remarks>
    public uint Layers;
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

/// <summary>Where a moved shape is blocked (<see cref="PhysicsWorld.MoveCast"/>).</summary>
public struct MoveCastHit
{
    /// <summary>The entity of the body that blocked the shape.</summary>
    public Entity Body;
    /// <summary>The entity of the collider that blocked it.</summary>
    public Entity Collider;
    /// <summary>Where the shape touches the body, on the shape, in world space.</summary>
    public Vector3 Point;
    /// <summary>The body's surface normal where it was touched, pointing towards the shape.</summary>
    public Vector3 Normal;
    /// <summary>How far the shape can travel along the motion and stay clear of the body: the safe distance.</summary>
    public float Distance;
}

/// <summary>A body a shape overlaps, and how to get out of it (<see cref="PhysicsWorld.Penetrations"/>).</summary>
public struct PenetrationHit
{
    /// <summary>The entity of the body.</summary>
    public Entity Body;
    /// <summary>The entity of the collider.</summary>
    public Entity Collider;
    /// <summary>The shape's deepest point inside the collider, in world space.</summary>
    public Vector3 Point;
    /// <summary>The unit direction that moves the shape out of the collider along the shortest path.</summary>
    public Vector3 Normal;
    /// <summary>How far along <see cref="Normal"/> the shape must move to stop overlapping the collider: more than 0.</summary>
    public float Depth;
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
    /// <summary>The body entity (a rigid body or a character body) asked about, and its collider touching.</summary>
    public Entity Body;
    public Entity Collider;
    /// <summary>The body entity (a rigid body or a character body) it touches, and that body's collider.</summary>
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
    /// <summary>Each joint's two frames as axes: frameA long (red, green, blue), frameB short (pink, yellow,
    /// cyan). They coincide at the rest pose and while the joint holds.</summary>
    Joints = 1u << 4,
    /// <summary>Each active character's shape in use (the crouched one while a stand-up is blocked), its ground
    /// normal coloured by ground state, and its moved velocity.</summary>
    Characters = 1u << 5,
    /// <summary>Each wheel of each active, enabled vehicle: its suspension, the wheel with one spoke, and its contact
    /// coloured by grip (gripping, past its tyre's peak slip, or past its slide slip).</summary>
    Vehicles = 1u << 6,
    /// <summary>Every category.</summary>
    All = Colliders | Contacts | Velocities | Bounds | Joints | Characters | Vehicles,
}
