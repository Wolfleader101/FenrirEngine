using Fenrir.Scripting;

namespace Fenrir.Physics;

// The hand-written parts of FenrirPhysics's joints (FenrirPhysics/Joints.hpp, ADR-0013). The fields, defaults and docs
// are in the generated mirrors (Generated/), checked against the native layout whenever a script assembly loads, as the
// components are; the C++ documentation is the reference for what each field means and how out-of-range values are
// corrected. Each has the C++ defaults when made with `new`. JointKind never crosses from C++ to C# by value (in an
// event or a call's argument), so the bools inside its kinds are safe: C# reads the component in place.

/// <remarks>Author one from C# with <see cref="Joint.FrameFromWorld(Entity, Entity, JointFrame)"/>.</remarks>
public partial struct JointFrame
{
}

/// <remarks>From C#, <see cref="PhysicsWorld.GetJointState"/> reads it.</remarks>
public partial struct JointState
{
}

/// <remarks>
/// From C#: laid out as the native <c>Fenrir::JointKind</c> (a one-byte tag, then the kinds overlapping at the union's
/// offset, 4 because every kind holds at most 4-byte values). Assigning a kind changes it; each kind's view reads or
/// edits it in place and throws <see cref="InvalidOperationException"/> if the joint is another kind. New, it is a fixed
/// joint.
/// </remarks>
public partial struct JointKind
{
}

/// <remarks>
/// From C#: link the bodies by assigning entities (<c>joint.BodyA = door</c>), including ones made by
/// <see cref="Entity.Create"/> earlier in the same system. Assign a kind to change it
/// (<c>joint.Kind = new HingeJoint()</c>) and edit one in place through its view
/// (<c>joint.Kind.Hinge.Motor.TargetVelocity = 2</c>). <c>new Joint()</c> has both links empty
/// (<see cref="EntityLink.None"/>). <c>default(Joint)</c> is all zero bytes, as any struct: its links name entity 0 with
/// no scene, so physics treats it as an inactive joint, with a warning.
/// </remarks>
public partial struct Joint
{
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
