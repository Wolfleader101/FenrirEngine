using System.Runtime.InteropServices;
using Fenrir.Scripting;

namespace Fenrir.Physics;

// The C# mirrors of FenrirPhysics's joints (FenrirPhysics/Joints.hpp, ADR-0013), checked against the native layout
// whenever a script assembly loads, as the components in Components.cs are. The C++ documentation is the reference for
// what each field means and how out-of-range values are corrected. Each has the C++ defaults when made with `new`.
// JointKind never crosses from C++ to C# by value (in an event or a call's argument), so the bools inside its kinds are
// safe: C# reads the component in place.

/// <summary>
/// A pose on a body: local to the body entity's origin, or in world space for the world end of a joint. Its local X
/// axis is a hinge's axis, a slider's direction and a swing-twist joint's twist axis. Author it with
/// <see cref="Joint.FrameFromWorld(Entity, Entity, JointFrame)"/>.
/// </summary>
public struct JointFrame
{
    public Vector3 Position;
    public Quaternion Rotation = Quaternion.Identity;

    public JointFrame()
    {
    }
}

/// <summary>Both frames of a joint, as <see cref="Joint.FrameFromWorld(Entity, Entity, JointFrame)"/> computes them.</summary>
public struct JointFrames
{
    public JointFrame FrameA = new();
    public JointFrame FrameB = new();

    public JointFrames()
    {
    }
}

/// <summary>
/// How a joint sits and how hard it pulls, as of the last step, from <see cref="PhysicsWorld.GetJointState"/>. The load
/// is the average over the step's last collision step: approximate within a step, exact enough for steady loads.
/// </summary>
public struct JointState
{
    /// <summary>Newtons, world space: what the joint applies to BodyB (BodyA gets the negative).</summary>
    public Vector3 Force;
    /// <summary>Newton metres, world space, about the joint's anchor: what the joint applies to BodyB, likewise.</summary>
    public Vector3 Torque;
    /// <summary>
    /// FrameB's pose in FrameA's axes: the identity is the rest pose. For a swing-twist joint, its rotation is in the
    /// same terms as its motor's <see cref="SwingTwistMotor.TargetRotation"/>.
    /// </summary>
    public JointFrame Relative = new();
    /// <summary>
    /// A hinge's angle (radians, wrapped to (-pi, pi]), a slider's offset along FrameA's X (metres), a distance joint's
    /// length (metres); 0 for the other kinds. A hinge's angle is FrameB's turn relative to FrameA, so its sign follows
    /// which body is A.
    /// </summary>
    public float Position;

    public JointState()
    {
    }
}

/// <summary>How hard a part of a joint pulls back.</summary>
public struct JointSpring
{
    /// <summary>Hertz: 0 is rigid.</summary>
    public float Frequency;
    /// <summary>The damping ratio: 1 is critical.</summary>
    public float Damping = 1;

    public JointSpring()
    {
    }
}

/// <summary>The range a hinge or slider is kept in, measured from the rest pose. Disabled, it is free.</summary>
public struct JointLimits
{
    public bool Enabled;
    /// <summary>Radians for a hinge, metres for a slider.</summary>
    public float Min;
    /// <summary>Radians for a hinge, metres for a slider.</summary>
    public float Max;
    /// <summary>Makes the limit soft.</summary>
    public JointSpring Spring = new();

    public JointLimits()
    {
    }
}

/// <summary>What a motor drives: nothing, a velocity or a position.</summary>
public enum JointMotorMode : byte
{
    Off,
    Velocity,
    Position,
}

/// <summary>Drives a hinge's angle or a slider's offset, measured from the rest pose. A hard limit always stops it.</summary>
public struct JointMotor
{
    public JointMotorMode Mode;
    /// <summary>Radians or metres per second, read in Velocity mode only.</summary>
    public float TargetVelocity;
    /// <summary>Radians or metres, read in Position mode only.</summary>
    public float TargetPosition;
    /// <summary>Newton metres for a hinge, newtons for a slider, either way.</summary>
    public float MaxForce = float.PositiveInfinity;
    /// <summary>How hard Position mode drives.</summary>
    public JointSpring Spring = new() { Frequency = 2, Damping = 1 };

    public JointMotor()
    {
    }
}

/// <summary>Drives a swing-twist joint's rotation, swing and twist together. Its cone and twist limits always stop it.</summary>
public struct SwingTwistMotor
{
    public JointMotorMode Mode;
    /// <summary>FrameB's rotation relative to FrameA, read in Position mode only: the identity is the rest pose.</summary>
    public Quaternion TargetRotation = Quaternion.Identity;
    /// <summary>Radians per second in FrameA's axes, read in Velocity mode only.</summary>
    public Vector3 TargetAngularVelocity;
    /// <summary>Newton metres, about each axis.</summary>
    public float MaxTorque = float.PositiveInfinity;
    /// <summary>How hard Position mode drives.</summary>
    public JointSpring Spring = new() { Frequency = 2, Damping = 1 };

    public SwingTwistMotor()
    {
    }
}

// The engine never reads these bytes; they stand for the one byte an empty C++ struct takes.
#pragma warning disable CS0169

/// <summary>Holds the two frames together in position and rotation: the bodies move as one.</summary>
public struct FixedJoint
{
    private byte _unused;
}

/// <summary>Holds the two frames' positions together and lets the bodies turn freely about them.</summary>
public struct BallJoint
{
    private byte _unused;
}

#pragma warning restore CS0169

/// <summary>
/// Keeps the two frames' positions between <see cref="MinDistance"/> and <see cref="MaxDistance"/> apart, and ignores the
/// frames' rotations: a rod at min == max, a rope at min 0, a spring when soft.
/// </summary>
public struct DistanceJoint
{
    /// <summary>Metres.</summary>
    public float MinDistance;
    /// <summary>Metres.</summary>
    public float MaxDistance = 1;
    /// <summary>Makes the distance soft: it stretches beyond the range and springs back.</summary>
    public JointSpring Spring = new();

    public DistanceJoint()
    {
    }
}

/// <summary>
/// Holds the two frames together but for turning about their shared X axis. The hinge angle is FrameB's turn relative to
/// FrameA, signed by the right-hand rule about FrameA's X.
/// </summary>
public struct HingeJoint
{
    /// <summary>Radians.</summary>
    public JointLimits Limits = new();
    public JointMotor Motor = new();
    /// <summary>The most torque, in newton metres, resisting the hinge's turn while its motor is off.</summary>
    public float Friction;

    public HingeJoint()
    {
    }
}

/// <summary>Holds the two frames together but for sliding along their shared X axis, without turning.</summary>
public struct SliderJoint
{
    /// <summary>Metres.</summary>
    public JointLimits Limits = new();
    public JointMotor Motor = new();
    /// <summary>The most force, in newtons, resisting the slide while its motor is off.</summary>
    public float Friction;

    public SliderJoint()
    {
    }
}

/// <summary>
/// Holds the two frames' positions together and keeps FrameB's turn relative to FrameA within hard limits, in radians: a
/// swing about Y and Z within a cone about FrameA's X, and a twist about X within a range. A shoulder or a hip.
/// </summary>
public struct SwingTwistJoint
{
    /// <summary>The cone's half-angle about Y.</summary>
    public float SwingYLimit = MathF.PI / 4;
    /// <summary>The cone's half-angle about Z.</summary>
    public float SwingZLimit = MathF.PI / 4;
    public float TwistMin = -MathF.PI / 4;
    public float TwistMax = MathF.PI / 4;
    /// <summary>The most torque, in newton metres, resisting the joint's turn while its motor is off.</summary>
    public float Friction;
    public SwingTwistMotor Motor = new();

    public SwingTwistJoint()
    {
    }
}

/// <summary>Which kind of joint a <see cref="JointKind"/> holds.</summary>
public enum JointType : byte
{
    Fixed,
    Ball,
    Distance,
    Hinge,
    Slider,
    SwingTwist,
}

/// <summary>
/// A joint kind: exactly one kind of joint and its settings, by value, laid out as the native <c>Fenrir::JointKind</c> (a
/// one-byte tag, then the kinds overlapping at the union's offset, 4 because every kind holds at most 4-byte values).
/// Assigning a kind changes it; each kind's view reads or edits it in place and throws
/// <see cref="InvalidOperationException"/> if the joint is another kind. New, it is a fixed joint.
/// </summary>
[TaggedUnion(Noun = "joint")]
[StructLayout(LayoutKind.Explicit)]
public partial struct JointKind
{
    [FieldOffset(0)] private JointType _type;
    [FieldOffset(4)] private FixedJoint _fixed;
    [FieldOffset(4)] private BallJoint _ball;
    [FieldOffset(4)] private DistanceJoint _distance;
    [FieldOffset(4)] private HingeJoint _hinge;
    [FieldOffset(4)] private SliderJoint _slider;
    [FieldOffset(4)] private SwingTwistJoint _swingTwist;
}

/// <summary>
/// Joins two bodies, or a body and the world (ADR-0013). Put it on a joint entity: any entity, one of the bodies it joins
/// included. The rest pose is where the two frames coincide in world space; author them with
/// <see cref="FrameFromWorld(Entity, Entity, JointFrame)"/>. Link the bodies by assigning entities
/// (<c>joint.BodyA = door</c>), including ones made by <see cref="Entity.Create"/> earlier in the same system. Assign a
/// kind to change it (<c>joint.Kind = new HingeJoint()</c>) and edit one in place through its view
/// (<c>joint.Kind.Hinge.Motor.TargetVelocity = 2</c>).
/// </summary>
/// <remarks>
/// <c>new Joint()</c> has both links empty (<see cref="EntityLink.None"/>). <c>default(Joint)</c> is all zero bytes, as
/// any struct: its links name entity 0 with no scene, so physics treats it as an inactive joint, with a warning.
/// </remarks>
[NativeComponent("Fenrir.Physics.Joint")]
public struct Joint : IComponent
{
    /// <summary>A body entity: a RigidBody's, or a standalone static collider's. Never the world.</summary>
    public EntityLink BodyA = EntityLink.None;
    /// <summary>A body entity, or <see cref="EntityLink.None"/>: the world.</summary>
    public EntityLink BodyB = EntityLink.None;
    /// <summary>Local to BodyA's origin.</summary>
    public JointFrame FrameA = new();
    /// <summary>Local to BodyB's origin, or in world space when BodyB is the world.</summary>
    public JointFrame FrameB = new();
    public JointKind Kind = new();
    /// <summary>A disabled joint holds nothing.</summary>
    public bool Enabled = true;
    /// <summary>Whether the two bodies collide with each other.</summary>
    public bool CollideConnected = false;
    /// <summary>Newtons.</summary>
    public float BreakForce = float.PositiveInfinity;
    /// <summary>Newton metres.</summary>
    public float BreakTorque = float.PositiveInfinity;

    public Joint()
    {
    }

    /// <summary>
    /// The frames that put a joint's rest pose at <paramref name="anchor"/> (world space), given where
    /// <paramref name="bodyA"/> and <paramref name="bodyB"/> are now, through their exact world matrices, as C++'s
    /// <c>Fenrir::FrameFromWorld</c> computes them. Computed once, when called; it needs the active scene, not a physics
    /// world. Never throws: a body that is not alive gives identity frames, with a warning in a debug build of the engine.
    /// A destroyed <paramref name="bodyB"/> is not the world; use <see cref="FrameFromWorld(Entity, JointFrame)"/> for that.
    /// </summary>
    public static JointFrames FrameFromWorld(Entity bodyA, Entity bodyB, JointFrame anchor)
    {
        PhysicsNative.FrameFromWorld(bodyA, bodyB, in anchor, out JointFrames frames);
        return frames;
    }

    /// <summary>
    /// The frames for a joint from <paramref name="bodyA"/> to the world, as
    /// <see cref="FrameFromWorld(Entity, Entity, JointFrame)"/> with an empty BodyB: FrameB is <paramref name="anchor"/>
    /// itself.
    /// </summary>
    public static JointFrames FrameFromWorld(Entity bodyA, JointFrame anchor)
    {
        // The native side reads the empty entity value as C++'s empty Entity, the world.
        PhysicsNative.FrameFromWorld(bodyA, new Entity(uint.MaxValue), in anchor, out JointFrames frames);
        return frames;
    }
}
