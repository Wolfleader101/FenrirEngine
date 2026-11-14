using Fenrir.Scripting;

namespace Fenrir.Ragdoll;

// Ragdoll's statics: PartCount, the span-out Build, Destroy and ClearOverrides are generated into the mirror
// (Generated/Ragdoll.cs, from the natives' @facade marks in Scripting/src/RagdollScripting.cpp). This part holds what a
// description cannot say: DescribeHumanoid, taking the 4 KB bone map by reference with C++'s default mass, and the Build
// that sizes its own arrays (PartCount, then the span-out Build) and gives them back as a SpawnedRagdoll.

/// <remarks>
/// <b>Building one from C#.</b> Fill a <see cref="HumanoidBones"/> with the rig's bone names, call
/// <see cref="DescribeHumanoid"/> on the skeleton entity, then <see cref="Build(Entity, RagdollDescriptionHandle)"/>: the
/// body and joint entities are alive at once, and get their components (and the skeleton entity its Ragdoll) when the
/// calling system returns, so build a skeleton's ragdoll once per system, and a <see cref="Destroy"/> right after the
/// Build in the same system does nothing. The skeleton must be tick-posed (<c>SkeletonPose.Timing = PoseTiming.Tick</c>): on a
/// frame-posed or not-ready skeleton the ragdoll is inactive, writes nothing and warns once. Edit the spawned entities
/// as any others (masses, shapes, joint limits); editing the description itself is C++'s only.
/// <para>
/// <b>Switching and weights.</b> Write <see cref="Mode"/> (and a body's own <see cref="RagdollBody.Mode"/>); the glue
/// applies it next tick, moving no body. A simulating body draws its bone only as far as the bone's override weight says,
/// and the glue never sets weights: set them to 1 when you drop the ragdoll (<c>Skeletons.SetOverrideWeight</c>).
/// </para>
/// <para>
/// <b>Partial ragdolls.</b> Set a subtree of bodies' <see cref="RagdollBody.Mode"/> to <see cref="RagdollBodyMode.Simulating"/>
/// (an arm gone limp) while the ragdoll follows, with that subtree's weights at 1
/// (<c>OverrideScope.BoneSubtree</c>); set them back to <see cref="RagdollBodyMode.Inherit"/> and ramp the weights to 0
/// to blend the arm back.
/// </para>
/// <para>
/// <b>Strength.</b> <see cref="Strength"/>, in [0, 1], drives powered bodies' motors towards the base pose, each body's
/// share scaled by its <see cref="RagdollBody.StrengthScale"/>. The base pose is whatever animation plays: swap the
/// character's locomotion clip for an idle or get-up pose before powering it, or its powered legs walk on the ground.
/// The root body is undriven, so a powered ragdoll keeps its shape but not its balance.
/// </para>
/// <para>
/// <b>Getting up.</b> Set <see cref="Mode"/> back to <see cref="RagdollMode.Following"/> where the body lies (the skeleton
/// entity has followed the root body), then ramp the override weights from 1 to 0 over a moment, in your own code: the
/// glue keeps the last simulated pose until every weight reads 0, then clears its entries. <see cref="ClearOverrides"/>
/// clears them at once.
/// </para>
/// </remarks>
public partial struct Ragdoll
{
    /// <summary>
    /// Derives a humanoid ragdoll description from the skeleton entity's skeleton asset and <paramref name="bones"/>
    /// (C++'s <c>DescribeHumanoid</c>, whose documentation gives the parts, capsules, masses and joints), kept for the
    /// active scene until <see cref="Build(Entity, RagdollDescriptionHandle)"/> takes it or the scene ends.
    /// </summary>
    /// <param name="skeleton">The skeleton entity, whose SkeletonPose names the asset.</param>
    /// <param name="bones">The humanoid bone map: the bone of each slot, by engine name; an empty slot drops its part
    /// and the chain below it.</param>
    /// <param name="totalMass">The ragdoll's mass in kilograms, shared out over the parts.</param>
    /// <returns>
    /// The description's handle; <see cref="RagdollDescriptionHandle.None"/> when the entity is not a Ready skeleton
    /// entity or the bone map is refused (a hips slot empty, a name no bone has, a slot not below its parent slot),
    /// warning once.
    /// </returns>
    public static RagdollDescriptionHandle DescribeHumanoid(Entity skeleton, in HumanoidBones bones,
                                                            float totalMass = 70.0f) =>
        RagdollNative.DescribeHumanoid(in skeleton, in bones, totalMass);

    /// <summary>
    /// Builds the description as the skeleton entity's ragdoll and drops the handle, sizing the entity arrays itself
    /// (<see cref="PartCount"/>, then the span-out <see cref="Build(Entity, RagdollDescriptionHandle, Span{Entity}, Span{Entity}, out Entity)"/>).
    /// </summary>
    /// <returns>
    /// The spawned entities, alive at once with their components added when the calling system returns; <c>null</c>
    /// when nothing was built (the handle kept unless it was no description or stale), as the span-out Build says.
    /// </returns>
    public static SpawnedRagdoll? Build(Entity skeleton, RagdollDescriptionHandle description)
    {
        int parts = PartCount(description);
        var bodies = new Entity[parts];
        var joints = new Entity[System.Math.Max(parts - 1, 0)];
        return Build(skeleton, description, bodies, joints, out Entity parent)
                   ? new SpawnedRagdoll(parent, bodies, joints)
                   : null;
    }
}

/// <summary>
/// What <see cref="Ragdoll.Build(Entity, RagdollDescriptionHandle)"/> spawned, as C++'s <c>SpawnedRagdoll</c>: game
/// code's to edit, query and draw, and <see cref="Ragdoll.Destroy"/>'s to remove.
/// </summary>
public sealed class SpawnedRagdoll
{
    internal SpawnedRagdoll(Entity parent, Entity[] bodies, Entity[] joints)
    {
        Parent = parent;
        Bodies = bodies;
        Joints = joints;
    }

    /// <summary>The entity the bodies and joints sit under, at the top of the scene.</summary>
    public Entity Parent { get; }

    /// <summary>One body entity per part, in the description's order: the root part's (the hips') first.</summary>
    public IReadOnlyList<Entity> Bodies { get; }

    /// <summary>One joint entity per part but the root, in the description's order.</summary>
    public IReadOnlyList<Entity> Joints { get; }
}
