using Fenrir.Math;
using Fenrir.Scripting;

namespace Fenrir.Skeleton;

/// <summary>
/// A pose edit (ADR-0033, ADR-0039): a <see cref="PoseStages.Modify"/> stage system's access to one skeleton instance's
/// final pose, from <see cref="Skeletons.Edit"/>. It holds the entity and an edit token, and each call is one native
/// call, as C++'s <c>PoseEdit</c>'s are. The token is good only during the stage system's group that opened it: kept and
/// used later, or opened outside Modify, an edit reads and writes nothing (and a debug build warns once).
/// </summary>
public readonly ref struct PoseEdit
{
    private readonly Entity _entity;
    private readonly ulong _token;

    internal PoseEdit(Entity entity, ulong token, bool ready)
    {
        _entity = entity;
        _token = token;
        IsEmpty = !ready;
    }

    /// <summary>The skeleton entity the edit is on.</summary>
    public Entity Entity => _entity;

    /// <summary>Whether the edit reached no Ready instance when it was opened, or was opened outside Modify.</summary>
    public bool IsEmpty { get; }

    /// <summary>The bone's base-pose local transform, as Animate left it.</summary>
    /// <returns>Whether it read: false for an empty or stale edit or a bone out of range.</returns>
    public bool Base(int bone, out Transform local) => SkeletonNative.EditBase(_token, in _entity, bone, out local);

    /// <summary>The bone's current local transform in the final pose, with every write so far applied.</summary>
    /// <returns>Whether it read: false for an empty or stale edit or a bone out of range.</returns>
    public bool Local(int bone, out Transform local) => SkeletonNative.EditLocal(_token, in _entity, bone, out local);

    /// <summary>The bone's current model-space matrix, with every write so far applied.</summary>
    /// <returns>Whether it read: false for an empty or stale edit or a bone out of range.</returns>
    public bool Model(int bone, out Matrix4x4 model) => SkeletonNative.EditModel(_token, in _entity, bone, out model);

    /// <summary>Writes <paramref name="target"/> to the bone now: later writes and later modifiers see it.</summary>
    /// <returns>
    /// Whether it wrote: false for an empty or stale edit, a bone out of range, or an inverse that does not exist.
    /// </returns>
    public bool Write(int bone, Target target) => SkeletonNative.EditWrite(_token, in _entity, bone, in target);
}
