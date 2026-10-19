using Fenrir.Scripting;

namespace Fenrir.Physics;

// The C# mirrors of FenrirPhysics's character body (FenrirPhysics/Characters.hpp, ADR-0014), checked against the native
// layout whenever a script assembly loads, as the components in Components.cs are. The C++ documentation is the
// reference for each rule; these comments carry what a C# controller needs. Each has the C++ defaults when made with
// `new`. The state's bools are read in place and CharacterState never crosses from C++ by value, so they are safe.

/// <summary>What a character body stood on at the end of its last move.</summary>
public enum GroundState : byte
{
    /// <summary>Touching nothing.</summary>
    InAir,
    /// <summary>Standing on walkable ground: no steeper than its <see cref="CharacterBody.MaxSlope"/>.</summary>
    OnGround,
    /// <summary>Standing on ground steeper than its <see cref="CharacterBody.MaxSlope"/>.</summary>
    OnSteepGround,
    /// <summary>Touching something, a wall or a ledge, with nothing underneath.</summary>
    Touching,
}

/// <summary>
/// How a character body's last move left it: written by physics into <see cref="CharacterBody.State"/> after every move,
/// against the bodies' poses before that tick's step. C# reads it in place and cannot write it. Landing and coyote time
/// are game code: keep last tick's <see cref="Ground"/> in a field or component of your own.
/// </summary>
public readonly struct CharacterState
{
    public readonly GroundState Ground;
    /// <summary>The body entity of the ground (a rigid body or a character body), on ground only; <see cref="EntityLink.None"/> otherwise.</summary>
    public readonly EntityLink GroundBody = EntityLink.None;
    /// <summary>
    /// The collider entity of the ground touched, on ground only; <see cref="EntityLink.None"/> otherwise. Its
    /// <see cref="PhysicsMaterial"/> is how game code makes ice.
    /// </summary>
    public readonly EntityLink GroundCollider = EntityLink.None;
    /// <summary>World space, where the character touches its ground, on ground only.</summary>
    public readonly Vector3 GroundPoint;
    /// <summary>World space, unit length, up from the ground, averaged over the ground contacts, on ground only.</summary>
    public readonly Vector3 GroundNormal;
    /// <summary>
    /// Metres per second, the ground's velocity at the character, on ground only. Physics adds it to the next move on
    /// walkable ground, never to <see cref="CharacterBody.Velocity"/>.
    /// </summary>
    public readonly Vector3 GroundVelocity;
    /// <summary>
    /// Radians per second, world space, the ground's angular velocity, on ground only. Physics never turns the character
    /// with its ground: turn it by this to turn it with a turntable.
    /// </summary>
    public readonly Vector3 GroundAngularVelocity;
    /// <summary>
    /// Metres per second, the last move's displacement over the tick's time: how fast the character actually moved,
    /// which <see cref="CharacterBody.Velocity"/> does not say.
    /// </summary>
    public readonly Vector3 MovedVelocity;
    /// <summary>
    /// Metres per second, the velocity of the walkable ground the character left, added to each move until it lands. A
    /// teleport clears it. It is never in <see cref="CharacterBody.Velocity"/>; <see cref="MovedVelocity"/> includes it.
    /// </summary>
    public readonly Vector3 CarriedVelocity;
    /// <summary>Touching a wall at the end of the move: a surface too steep to stand on that is not its ground.</summary>
    public readonly bool OnWall;
    /// <summary>Touching a ceiling at the end of the move: a surface facing down.</summary>
    public readonly bool OnCeiling;
    /// <summary>
    /// The <see cref="Collider"/>'s shape or scale does not fit where the character is yet, so it keeps the old one.
    /// Physics retries each move and clears this once the new one fits.
    /// </summary>
    public readonly bool ShapeBlocked;

    public CharacterState()
    {
    }
}

/// <summary>
/// Makes the entity's own <see cref="Collider"/> a character body (ADR-0014): collide-and-slide, moved by
/// <see cref="Velocity"/>, and met by bodies, queries and triggers as a kinematic body of its shape, named by its entity.
/// There is no move call: a <see cref="Phase.Tick"/> system writes <see cref="Velocity"/> through
/// <c>Ref&lt;CharacterBody&gt;</c> and reads <see cref="State"/> through the same reference, and physics moves the
/// character once each tick after the Tick systems, before the step.
/// </summary>
/// <remarks>
/// <para>
/// <b>Moving.</b> Physics writes <see cref="Velocity"/> back with what went into the ground, walls and ceilings removed,
/// so landing clears falling speed. Gravity is game code's: add <c>PhysicsWorld.Gravity * dt</c> each tick, and set
/// <c>Velocity.Y</c> to jump. To slide down a slope with <see cref="StopOnSlope"/> off, add input to the
/// <see cref="Velocity"/> physics wrote back rather than overwriting it, or the character only creeps.
/// <see cref="CharacterState.MovedVelocity"/>, not <see cref="Velocity"/>, is how fast it actually moved. Writing the
/// <see cref="Transform"/>'s position teleports it.
/// </para>
/// <para>
/// <b>Other bodies.</b> A move pushes the dynamic bodies it hits, with at most <see cref="PushForce"/> on each, and a
/// character on ground presses it with <see cref="Mass"/>. Dynamic bodies never push a character: knockback is game code
/// writing <see cref="Velocity"/>. A kinematic body moving into a character shoves it at the body's own speed, which
/// shows in <see cref="Velocity"/>. Characters use <see cref="ContactBegin"/>, <see cref="ContactEnd"/>,
/// <see cref="TriggerEnter"/> and <see cref="TriggerExit"/> like bodies, and <see cref="PhysicsWorld.ContactsOf"/> lists
/// a character's contacts.
/// </para>
/// <para>
/// <b>Crouching.</b> Write the entity's <see cref="Collider.Shape"/> once; physics never needs it written again. On ground
/// a crouch keeps the feet down, and in the air it keeps the head in place. <see cref="StepHeight"/> does not follow the
/// shape. A stand-up that does not fit yet keeps the old shape and sets <see cref="CharacterState.ShapeBlocked"/>, which
/// stays true until it fits.
/// </para>
/// <para>
/// <b>Inactive.</b> Beside a <see cref="RigidBody"/>, with no <see cref="Collider"/> or with a <see cref="MeshShape"/> it
/// is inactive, with one warning each time it becomes so. While disabled or inactive, nothing is written back but
/// <see cref="State"/>, which reads as the defaults, and queries do not see it. Nothing throws.
/// </para>
/// <para>
/// Make it with <c>new</c>. <c>default(CharacterBody)</c> is all zero bytes: a disabled character with every rule off,
/// and no warning.
/// </para>
/// </remarks>
[NativeComponent("Fenrir.Physics.CharacterBody")]
public struct CharacterBody : IComponent
{
    /// <summary>World space, metres per second: read fresh every tick and written back trimmed by what the character touches.</summary>
    public Vector3 Velocity;
    /// <summary>Radians (45°): ground steeper than this is not walkable. 0 turns slope limiting off.</summary>
    public float MaxSlope = 0.785f;
    /// <summary>Metres: the highest step the character walks up. 0 turns stair walking off.</summary>
    public float StepHeight = 0.4f;
    /// <summary>Metres: how far down the character sticks to the ground walking down slopes and steps. 0 turns it off.</summary>
    public float FloorSnap = 0.5f;
    /// <summary>Newtons: the most force a move's push applies to each dynamic body it hits. 0 turns pushing off.</summary>
    public float PushForce = 100;
    /// <summary>Kilograms: only the character's weight on its ground. 0 turns it off.</summary>
    public float Mass = 70;
    /// <summary>On walkable ground, the falling part of <see cref="Velocity"/> does not slide the character down a slope.</summary>
    public bool StopOnSlope = true;
    /// <summary>Between ticks the Transform is blended between the last two steps, while Tick systems see the true one.</summary>
    public bool Interpolate = true;
    /// <summary>
    /// Disabled: not moved, invisible to bodies, queries and triggers, <see cref="Velocity"/> and the Transform left to
    /// game code, and <see cref="State"/> reset.
    /// </summary>
    public bool Enabled = true;
    /// <summary>Read-back, overwritten by physics after every move: read it through <c>Ref&lt;CharacterBody&gt;</c>, never write it.</summary>
    public readonly CharacterState State = new();

    public CharacterBody()
    {
    }
}
