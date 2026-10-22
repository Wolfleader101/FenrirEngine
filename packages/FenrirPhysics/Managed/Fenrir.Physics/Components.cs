using Fenrir.Math;
using Fenrir.Scripting;

namespace Fenrir.Physics;

// The C# mirrors of FenrirPhysics's components (FenrirPhysics/Components.hpp), every one writable. Each is checked against
// its native layout whenever a script assembly loads, so a drift between the two fails the load and names the field. The
// C++ documentation is the reference for what each field means; edits take effect as they do in C++ (physics finds them
// by comparing, each tick). Each has the C++ defaults when made with `new`; `default` is all zero bytes, as in any struct.

/// <summary>How a rigid body moves: never (static), by game code (kinematic) or by the simulation (dynamic).</summary>
public enum MotionType : byte
{
    Static,
    Kinematic,
    Dynamic,
}

/// <summary>
/// Makes the entity's collider a rigid body the physics world simulates. Writing a dynamic body's velocities sets them;
/// writing its <see cref="Transform"/> teleports it.
/// </summary>
[NativeComponent("Fenrir.Physics.RigidBody")]
public struct RigidBody : IComponent
{
    public MotionType MotionType = MotionType.Dynamic;
    /// <summary>Kilograms. Used by dynamic bodies.</summary>
    public float Mass = 1;
    /// <summary>How fast linear velocity decays on its own, per second.</summary>
    public float LinearDamping = 0.05f;
    /// <summary>How fast angular velocity decays on its own, per second.</summary>
    public float AngularDamping = 0.05f;
    /// <summary>Multiplies the world's gravity for this body.</summary>
    public float GravityScale = 1;
    /// <summary>Metres per second, in world space.</summary>
    public Vector3 LinearVelocity;
    /// <summary>Radians per second about each world axis.</summary>
    public Vector3 AngularVelocity;
    /// <summary>Whether the body is shown between its last two step poses between ticks.</summary>
    public bool Interpolate = true;

    public RigidBody()
    {
    }
}

/// <summary>
/// Gives the entity collision geometry. Assign a kind to change it (<c>collider.Shape = new SphereShape { Radius = 1 }</c>)
/// and edit one in place through its view (<c>collider.Shape.Box.HalfExtents.Y = 2</c>).
/// </summary>
[NativeComponent("Fenrir.Physics.Collider")]
public struct Collider : IComponent
{
    public CollisionShape Shape = new();

    public Collider()
    {
    }

    public Collider(CollisionShape shape) => Shape = shape;
}

/// <summary>How a collider's surface responds to contact.</summary>
[NativeComponent("Fenrir.Physics.PhysicsMaterial")]
public struct PhysicsMaterial : IComponent
{
    /// <summary>How much the surface resists sliding: 0 is ice, about 1 is rubber on concrete.</summary>
    public float Friction = 0.5f;
    /// <summary>How much of its speed into a surface a body keeps when it bounces off.</summary>
    public float Restitution;

    public PhysicsMaterial()
    {
    }
}

/// <summary>Which of the 32 collision layers a body is in, and which layers it collides with.</summary>
[NativeComponent("Fenrir.Physics.CollisionFilter")]
public struct CollisionFilter : IComponent
{
    /// <summary>The layers the body is in, one bit per layer.</summary>
    public uint Layers = 1;
    /// <summary>The layers the body collides with, one bit per layer.</summary>
    public uint Mask = uint.MaxValue;

    public CollisionFilter()
    {
    }
}

/// <summary>Makes the entity's collider a trigger: it reports what overlaps it and never pushes anything.</summary>
[NativeComponent("Fenrir.Physics.Trigger")]
public struct Trigger : IComponent
{
    /// <summary>A disabled trigger overlaps nothing.</summary>
    public bool Enabled = true;

    public Trigger()
    {
    }
}

/// <summary>
/// Reports the body's contacts as events. Put it on the body entity (a rigid body or a character body).
/// </summary>
[NativeComponent("Fenrir.Physics.ContactEvents")]
public struct ContactEvents : IComponent
{
    /// <summary>Disabled, the body's contacts are not reported.</summary>
    public bool Enabled = true;

    public ContactEvents()
    {
    }
}
