namespace Fenrir.Skeleton;

/// <summary>
/// FenrirSkeleton's pose stages (ADR-0033, ADR-0039), which C# systems join with
/// <c>[StageSystem(PoseStages.Modify, Order = ...)]</c>: each runs inside the pose pipeline, by its order among the C++
/// work of its stage, after C++ work of an equal order. Structural changes a stage system records happen when the whole
/// pass returns. Without FenrirSkeleton installed, or in an inactive scene, they never run.
/// </summary>
public static class PoseStages
{
    /// <summary>
    /// Base-pose writers: run in the frame pass and in each tick pass, writing the skeletons of that pass's timing
    /// (<see cref="Timing"/>) through <c>Skeletons.SetBaseLocal</c> and <c>Skeletons.SetBasePose</c>.
    /// </summary>
    public const string Animate = "Fenrir.Skeleton.Animate";

    /// <summary>Pose modifiers: edit the final pose through <see cref="Skeletons.Edit"/>, once a frame.</summary>
    public const string Modify = "Fenrir.Skeleton.Modify";

    /// <summary>
    /// After the pose is final and attachments are placed: reads this frame's bones and writes ordinary ECS state (a
    /// camera following a bone), never poses.
    /// </summary>
    public const string Late = "Fenrir.Skeleton.Late";

    /// <summary>
    /// The running pass's timing, inside an <see cref="Animate"/> stage system: <see cref="PoseTiming.Frame"/> in the
    /// frame pass, <see cref="PoseTiming.Tick"/> in a tick pass. Frame anywhere else.
    /// </summary>
    public static PoseTiming Timing => SkeletonNative.GetTiming();

    /// <summary>
    /// The running pass's step in seconds, inside an <see cref="Animate"/> stage system: the frame's delta in the frame
    /// pass, the tick rate in a tick pass. 0 anywhere else.
    /// </summary>
    public static double Step => SkeletonNative.GetStep();
}
