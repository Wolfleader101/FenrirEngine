using Fenrir.Math;
using Fenrir.Scripting;
using Fenrir.Skeleton;

namespace Fenrir.SkeletonScriptFixture;

// Stage systems in each pose stage. They act only while a test has armed them on a skeleton entity (Probe.Arm), and
// keep what they saw in Probe, which the tests read through Iris.
public static class Probe
{
    public static uint Skeleton = uint.MaxValue;
    public static int Bone = -1;
    public static uint Follower = uint.MaxValue;

    // What the Modify system saw of the bone's local X before writing its own, and whether its writes went through.
    public static float ModifierSaw = float.NaN;
    public static bool ModifierWrote;
    // A token kept from one Modify run and written through in the next: it must write nothing.
    public static ulong KeptToken;
    public static bool StaleWrote;
    public static int StaleTries;

    // The Animate system's passes, by the timing PoseStages gave, and the last step of each.
    public static int FramePasses, TickPasses;
    public static double FrameStep, TickStep;
    // Whether the Animate system's base-pose write was taken, and refused, in each pass.
    public static int AnimateWrites, AnimateRefusals;
    public static bool WriteInAnimate;

    // Arms the systems on `skeleton`'s `bone`; the Late system then places `follower` at the bone each frame.
    public static void Arm(uint skeleton, int bone, uint follower)
    {
        Skeleton = skeleton;
        Bone = bone;
        Follower = follower;
        ModifierSaw = float.NaN;
        ModifierWrote = false;
        KeptToken = 0;
        StaleWrote = false;
        StaleTries = 0;
        FramePasses = TickPasses = 0;
        FrameStep = TickStep = 0;
        AnimateWrites = AnimateRefusals = 0;
        WriteInAnimate = false;
    }

    public static void Disarm() => Skeleton = uint.MaxValue;

    public static void WriteInAnimateStage(bool on) => WriteInAnimate = on;

    public static float GetModifierSaw() => ModifierSaw;
    public static bool GetModifierWrote() => ModifierWrote;
    public static bool GetStaleWrote() => StaleWrote;
    public static int GetStaleTries() => StaleTries;
    public static int GetFramePasses() => FramePasses;
    public static int GetTickPasses() => TickPasses;
    public static double GetFrameStep() => FrameStep;
    public static double GetTickStep() => TickStep;
    public static int GetAnimateWrites() => AnimateWrites;
    public static int GetAnimateRefusals() => AnimateRefusals;

    internal static bool Armed => Skeleton != uint.MaxValue;
}

// Counts each pass by its timing; with WriteInAnimate, writes the bone's base local, which only a pass of the
// skeleton's own timing takes.
[StageSystem(PoseStages.Animate, Order = 10)]
public sealed class PassCounter : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        if (!Probe.Armed)
            return;
        if (PoseStages.Timing == PoseTiming.Tick)
        {
            ++Probe.TickPasses;
            Probe.TickStep = PoseStages.Step;
        }
        else
        {
            ++Probe.FramePasses;
            Probe.FrameStep = PoseStages.Step;
        }
        if (!Probe.WriteInAnimate)
            return;
        var local = new Transform { Position = new Vector3(0, 1, 0), Scale = new Vector3(1, 1, 1) };
        if (Skeletons.SetBaseLocal(new Entity(Probe.Skeleton), Probe.Bone, local))
            ++Probe.AnimateWrites;
        else
            ++Probe.AnimateRefusals;
    }
}

// Between the tests' C++ modifiers at 100 and 300: reads the bone's local X as the first left it, then moves it to 2.
[StageSystem(PoseStages.Modify, Order = 200)]
public sealed class Nudge : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        if (!Probe.Armed)
            return;
        var skeleton = new Entity(Probe.Skeleton);
        PoseEdit edit = Skeletons.Edit(skeleton);
        if (edit.Local(Probe.Bone, out Transform local))
            Probe.ModifierSaw = local.Position.X;
        local.Position = new Vector3(2, local.Position.Y, local.Position.Z);
        Probe.ModifierWrote = edit.Write(Probe.Bone, new Target { Space = PoseSpace.Local, Trs = local });

        // Last run's token, used now: a stale edit, which writes nothing.
        var moved = local;
        moved.Position = new Vector3(5, local.Position.Y, local.Position.Z);
        var stale = new Target { Space = PoseSpace.Local, Trs = moved };
        if (Probe.KeptToken != 0)
        {
            ++Probe.StaleTries;
            Probe.StaleWrote |= SkeletonNative.EditWrite(Probe.KeptToken, in skeleton, Probe.Bone, in stale);
        }
        SkeletonNative.EditOpen(in skeleton, out Probe.KeptToken);
    }
}

// After the pose is final: puts the follower at the bone's world place this frame.
[StageSystem(PoseStages.Late)]
public sealed class Follow : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        if (!Probe.Armed || Probe.Follower == uint.MaxValue)
            return;
        if (!Skeletons.GetBoneWorldMatrix(new Entity(Probe.Skeleton), Probe.Bone, out Matrix4x4 world))
            return;
        var follower = new Entity(Probe.Follower);
        Vector4 at = world.Column3;
        follower.Get<Transform>().Value.Position = new Vector3(at.X, at.Y, at.Z);
    }
}
