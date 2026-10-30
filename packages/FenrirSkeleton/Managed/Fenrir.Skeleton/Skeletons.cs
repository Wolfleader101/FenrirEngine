using Fenrir.Assets;
using Fenrir.Math;
using Fenrir.Scripting;

namespace Fenrir.Skeleton;

// The reads, writes, overrides, attachments and SpawnModel are generated (Generated/Skeletons.cs, from the natives'
// @facade marks in Scripting/src/SkeletonScripting.cpp), each taking every argument its native takes. This part holds
// what a description cannot say: the class doc, the overloads that default the pose kind, the scope and the parent, and
// Edit, which opens a PoseEdit.

/// <summary>
/// Skeleton instances from C#, as FenrirSkeleton's <c>Skeletons</c> does in C++ (whose documentation says what each
/// does): a skeleton entity is an entity with a <see cref="SkeletonPose"/>, its bones <c>int</c> indices with -1 for
/// none. Reads are per bone (<c>GetLocal</c>, <c>GetBoneWorldMatrix</c>) or copies of a whole pose into a caller's span
/// (<c>ReadLocals</c>, <c>ReadBaseLocals</c>, <c>ReadModel</c>): nothing spans FenrirSkeleton's own memory, so a copy
/// never goes stale.
/// </summary>
/// <remarks>
/// Base-pose writes (<c>SetBaseLocal</c>, <c>SetBasePose</c>) work before the pipeline, in a
/// <see cref="PoseStages.Animate"/> stage system on skeletons of its pass's timing, and after the pipeline (seen next
/// frame); in <see cref="PoseStages.Modify"/> and <see cref="PoseStages.Late"/> they write nothing. A Modify stage system
/// edits the final pose through <see cref="Edit"/>. Overrides, weights, attachment states, <c>Detach</c> and
/// <c>SpawnModel</c> work outside the pipeline; the last two are structural, so they happen when the calling system
/// returns.
/// <para>
/// Nothing here throws. On an entity that is not alive or not a Ready skeleton entity, a bone out of range or a stale
/// handle, a function does nothing and returns <c>false</c>, 0, -1 or the default, and a debug build of the engine warns
/// once per place in its native code that noticed. With FenrirSkeleton not installed no skeleton is Ready.
/// </para>
/// </remarks>
public static partial class Skeletons
{
    /// <summary>The bone's local transform in the render base pose.</summary>
    /// <returns>Whether it read: false when the entity is not Ready or the bone is out of range.</returns>
    public static bool GetBaseLocal(Entity entity, int bone, out Transform local) =>
        GetBaseLocal(entity, bone, PoseKind.Render, out local);

    /// <summary>The bone's model-space matrix in the render base pose.</summary>
    /// <returns>Whether it read: false when the entity is not Ready or the bone is out of range.</returns>
    public static bool GetBaseModel(Entity entity, int bone, out Matrix4x4 model) =>
        GetBaseModel(entity, bone, PoseKind.Render, out model);

    /// <summary>The bone's world matrix in the pose this frame draws.</summary>
    /// <returns>Whether it read: false when the entity is not Ready or the bone is out of range.</returns>
    public static bool GetBoneWorldMatrix(Entity entity, int bone, out Matrix4x4 world) =>
        GetBoneWorldMatrix(entity, bone, PoseKind.Render, out world);

    /// <summary>Copies the render base pose's local transforms, one per bone, into <paramref name="locals"/>.</summary>
    /// <returns>How many bones it wrote.</returns>
    public static int ReadBaseLocals(Entity entity, Span<Transform> locals) =>
        ReadBaseLocals(entity, locals, PoseKind.Render);

    /// <summary>Sets the bone's own override weight, clamped to [0, 1].</summary>
    /// <returns>Whether it was set.</returns>
    public static bool SetOverrideWeight(Entity entity, int bone, float weight) =>
        SetOverrideWeight(entity, bone, weight, OverrideScope.Bone);

    /// <summary>Sets the bone's own override weight back to 1.</summary>
    /// <returns>Whether it was set.</returns>
    public static bool ClearOverrideWeight(Entity entity, int bone) =>
        ClearOverrideWeight(entity, bone, OverrideScope.Bone);

    /// <summary>
    /// Detaches the entity from its bone, with <see cref="DetachMode.KeepWorld"/> to the top of the scene, when the
    /// system returns.
    /// </summary>
    /// <returns>What the three-argument Detach returns.</returns>
    public static DetachResult Detach(Entity entity, DetachMode mode) => Detach(entity, mode, Entity.None);

    /// <summary>Spawns <paramref name="model"/> with its skeletons at the top of the active scene.</summary>
    /// <returns>The model's root entity; <see cref="Entity.None"/> when nothing was spawned.</returns>
    public static Entity SpawnModel(AssetHandle<Model> model) => SpawnModel(model, Entity.None);

    /// <summary>
    /// Opens a pose edit on the skeleton entity, for the running <see cref="PoseStages.Modify"/> stage system: its only
    /// access to the final pose while Modify builds it. The edit is good until the system's group returns. Outside a
    /// Modify stage system, or on an entity that is not Ready, it is empty and writes nothing.
    /// </summary>
    public static PoseEdit Edit(Entity entity)
    {
        bool ready = SkeletonNative.EditOpen(in entity, out ulong token);
        return new PoseEdit(entity, token, ready);
    }
}
