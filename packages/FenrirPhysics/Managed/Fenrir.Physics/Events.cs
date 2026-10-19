using Fenrir.Scripting;

namespace Fenrir.Physics;

// The engine fills these fields in native memory; the compiler cannot see that.
#pragma warning disable CS0649

// The C# mirrors of the contact, trigger and joint events a physics world sends (FenrirPhysics/Events.hpp), once per step, in
// PostTick. Read them through an EventReader<T> field; each reader sees each event once. Each is checked against its native
// layout whenever a script assembly loads, so a drift between the two fails the load and names the field. The entities are
// links as the engine stores them (as in Relationship): read each one's Target. They may already be destroyed by the time an
// end or exit is read, so every begin and enter has its end and exit.

/// <summary>
/// Two solid colliders of different bodies began touching (a contact), and at least one of the bodies has an enabled
/// <see cref="ContactEvents"/>. A and B are ordered by entity, <see cref="BodyA"/> first. A body falling asleep does not end
/// a contact.
/// </summary>
[NativeEvent("Fenrir.Physics.ContactBegin")]
public readonly struct ContactBegin : IEvent
{
    /// <summary>The body entities (each a rigid body or a character body).</summary>
    public readonly EntityLink BodyA;
    public readonly EntityLink BodyB;
    /// <summary>The collider entities touching (each its body's entity, or one beneath it in a compound body).</summary>
    public readonly EntityLink ColliderA;
    public readonly EntityLink ColliderB;
    /// <summary>Where they touch, in world space.</summary>
    public readonly Vector3 Point;
    /// <summary>World space, unit length, pointing from A to B.</summary>
    public readonly Vector3 Normal;
    /// <summary>How fast A and B were closing along <see cref="Normal"/> when they began touching, in metres per second.</summary>
    public readonly float RelativeSpeed;
}

/// <summary>
/// A contact reported by <see cref="ContactBegin"/> ended: the colliders separated, one of the bodies or colliders was
/// removed, or <see cref="ContactEvents"/> was removed or disabled on both bodies. Names the same entities, in the same order,
/// as its <see cref="ContactBegin"/>.
/// </summary>
[NativeEvent("Fenrir.Physics.ContactEnd")]
public readonly struct ContactEnd : IEvent
{
    public readonly EntityLink BodyA;
    public readonly EntityLink BodyB;
    public readonly EntityLink ColliderA;
    public readonly EntityLink ColliderB;
}

/// <summary>A collider began overlapping an enabled <see cref="Trigger"/>.</summary>
[NativeEvent("Fenrir.Physics.TriggerEnter")]
public readonly struct TriggerEnter : IEvent
{
    /// <summary>The trigger's collider entity.</summary>
    public readonly EntityLink Trigger;
    /// <summary>The body entity (a rigid body or a character body) of the collider that entered.</summary>
    public readonly EntityLink OtherBody;
    /// <summary>The collider entity that entered.</summary>
    public readonly EntityLink OtherCollider;
}

/// <summary>
/// A collider that entered a trigger stopped overlapping it: it left, one of them was removed, or the trigger was disabled.
/// Names the same entities as its <see cref="TriggerEnter"/>. A body falling asleep inside a trigger does not exit.
/// </summary>
[NativeEvent("Fenrir.Physics.TriggerExit")]
public readonly struct TriggerExit : IEvent
{
    public readonly EntityLink Trigger;
    public readonly EntityLink OtherBody;
    public readonly EntityLink OtherCollider;
}

/// <summary>
/// A joint broke: in the last step its load went over its <see cref="Joint"/>'s <see cref="Joint.BreakForce"/> or
/// <see cref="Joint.BreakTorque"/>, so physics set its <see cref="Joint.Enabled"/> to false, which C# sees in the
/// component. Sent only for breaks, never when game code disables a joint. Sorted by joint entity.
/// </summary>
/// <remarks>
/// Read it every frame: a system that reads only now and then misses breaks. Repair a joint by setting
/// <see cref="Joint.Enabled"/> back to true; one still over its threshold breaks again on the next step, with another event.
/// </remarks>
[NativeEvent("Fenrir.Physics.JointBroken")]
public readonly struct JointBroken : IEvent
{
    /// <summary>The joint entity, whose Joint is now disabled.</summary>
    public readonly EntityLink Joint;
    /// <summary>The joint's BodyA.</summary>
    public readonly EntityLink BodyA;
    /// <summary>The joint's BodyB: <see cref="EntityLink.IsNull"/> for the world.</summary>
    public readonly EntityLink BodyB;
    /// <summary>Newtons, world space: what the joint applied to BodyB in the step that broke it (<see cref="JointState.Force"/>).</summary>
    public readonly Vector3 Force;
    /// <summary>Newton metres, world space, likewise (<see cref="JointState.Torque"/>).</summary>
    public readonly Vector3 Torque;
}
