using Fenrir.Assets;
using Fenrir.Math;
using Fenrir.Scripting;
using Fenrir.Skeleton;

namespace Fenrir.SkeletonScriptFixture;

// The reads, copies, writes, overrides and attachments from a script, each a thin call the tests make through Iris and
// compare with C++.
public static class Reads
{
    public static int PoseState(uint entity) => (int)Skeletons.GetPoseState(new Entity(entity));

    public static ulong Generation(uint entity) => Skeletons.GetPoseGeneration(new Entity(entity));

    public static int FindTip(uint entity) => Skeletons.FindBone(new Entity(entity), "tip");

    // One component of a per-bone read: the local's position Y, or the matrix's translation Y; NaN when it read nothing.
    public static float LocalY(uint entity, int bone) =>
        Skeletons.GetLocal(new Entity(entity), bone, out Transform local) ? local.Position.Y : float.NaN;

    public static float BaseLocalY(uint entity, int bone, int kind) =>
        Skeletons.GetBaseLocal(new Entity(entity), bone, (PoseKind)kind, out Transform local) ? local.Position.Y
                                                                                              : float.NaN;

    public static float ModelY(uint entity, int bone) =>
        Skeletons.GetModel(new Entity(entity), bone, out Matrix4x4 model) ? model.Column3.Y : float.NaN;

    public static float WorldY(uint entity, int bone, int kind) =>
        Skeletons.GetBoneWorldMatrix(new Entity(entity), bone, (PoseKind)kind, out Matrix4x4 world) ? world.Column3.Y
                                                                                                    : float.NaN;

    // The copies: how many bones each wrote into a span of `room`, and the bone's value from it.
    public static int ReadLocalsCount(uint entity, int room) => Skeletons.ReadLocals(new Entity(entity), new Transform[room]);

    public static float ReadLocalsY(uint entity, int bone)
    {
        var locals = new Transform[16];
        return bone < Skeletons.ReadLocals(new Entity(entity), locals) ? locals[bone].Position.Y : float.NaN;
    }

    public static float ReadBaseLocalsY(uint entity, int bone, int kind)
    {
        var locals = new Transform[16];
        return bone < Skeletons.ReadBaseLocals(new Entity(entity), locals, (PoseKind)kind) ? locals[bone].Position.Y
                                                                                          : float.NaN;
    }

    public static float ReadModelY(uint entity, int bone)
    {
        var models = new Matrix4x4[16];
        return bone < Skeletons.ReadModel(new Entity(entity), models) ? models[bone].Column3.Y : float.NaN;
    }

    // Writes the bone's base local position to (0, y, 0).
    public static bool SetBaseY(uint entity, int bone, float y) =>
        Skeletons.SetBaseLocal(new Entity(entity), bone,
                               new Transform { Position = new Vector3(0, y, 0), Scale = new Vector3(1, 1, 1) });

    // Writes every bone's base local position to (0, y, 0), as one span.
    public static int SetBasePoseY(uint entity, int bones, float y)
    {
        var pose = new Transform[bones];
        for (int i = 0; i < bones; ++i)
            pose[i] = new Transform { Position = new Vector3(0, y, 0), Scale = new Vector3(1, 1, 1) };
        return Skeletons.SetBasePose(new Entity(entity), pose);
    }

    // A model-space override putting the bone at (x, y, 0), and its subtree's weight.
    public static bool OverrideAt(uint entity, int bone, float x, float y) =>
        Skeletons.SetOverride(new Entity(entity), bone,
                              new Target { Space = PoseSpace.Model, Channels = Channels.Translation,
                                           Trs = new Transform { Position = new Vector3(x, y, 0) } });

    public static bool OverrideWeight(uint entity, int bone, float weight) =>
        Skeletons.SetOverrideWeight(new Entity(entity), bone, weight, OverrideScope.BoneSubtree);

    public static float GetOverrideWeight(uint entity, int bone) => Skeletons.GetOverrideWeight(new Entity(entity), bone);

    public static bool ClearOverrides(uint entity) => Skeletons.ClearOverrides(new Entity(entity));

    // A pose edit outside Modify: empty, and its write takes nothing.
    public static bool EditOutsideModify(uint entity, int bone)
    {
        PoseEdit edit = Skeletons.Edit(new Entity(entity));
        return !edit.IsEmpty || edit.Write(bone, new Target());
    }

    // PoseStages outside Animate: Frame (0) and a step of 0.
    public static int TimingOutsideAnimate() => (int)PoseStages.Timing;

    public static double StepOutsideAnimate() => PoseStages.Step;

    // The skeleton asset's queries, by its handle.
    public static int BoneCount(uint index, uint generation) =>
        SkeletonData.BoneCount(new AssetHandle<Fenrir.Assets.Skeleton>(index, generation));

    public static int Parent(uint index, uint generation, int bone) =>
        SkeletonData.Parent(new AssetHandle<Fenrir.Assets.Skeleton>(index, generation), bone);

    public static bool IsDescendant(uint index, uint generation, int bone, int ancestor) =>
        SkeletonData.IsDescendant(new AssetHandle<Fenrir.Assets.Skeleton>(index, generation), bone, ancestor);

    public static string? EngineName(uint index, uint generation, int bone) =>
        SkeletonData.EngineName(new AssetHandle<Fenrir.Assets.Skeleton>(index, generation), bone);

    public static int SubtreeSum(uint index, uint generation, int bone) =>
        SkeletonData.Subtree(new AssetHandle<Fenrir.Assets.Skeleton>(index, generation), bone).Sum() +
        SkeletonData.Subtree(new AssetHandle<Fenrir.Assets.Skeleton>(index, generation), bone).Length * 100;
}

// Attachments and spawning from a script.
public static class Attaching
{
    // The entity of the active scene with this value, found as a script finds one, through a query (every entity has a
    // Transform), so it carries its scene: a component field naming an entity by id alone names none.
    private static Entity Owned(uint value)
    {
        foreach (var (entity, _) in Fenrir.Scripting.World.Query<Transform>())
            if (entity.Value == value)
                return entity;
        return Entity.None;
    }

    private static readonly string[] BoneNames = ["root", "mid", "tip", "no such bone"];

    // Gives `entity` a BoneAttachment to `skeleton`'s bone named BoneNames[name], one unit out along X.
    public static bool Attach(uint entity, uint skeleton, int name)
    {
        string bone = BoneNames[name];
        var attachment = new BoneAttachment
        {
            Skeleton = Owned(skeleton),
            Offset = new Transform { Position = new Vector3(1, 0, 0) },
        };
        if (!attachment.Bone.Set(bone))
            return false;
        new Entity(entity).Add(attachment);
        return true;
    }

    // A name over 255 bytes, refused: the field keeps its old name.
    public static string? RefusedName()
    {
        BoneName name = BoneName.Of("tip");
        bool set = name.Set(new string('x', 256));
        return set ? null : name.Get();
    }

    public static int State(uint entity) => (int)Skeletons.GetAttachmentState(new Entity(entity));

    public static int Detach(uint entity, int mode, uint parent) =>
        (int)Skeletons.Detach(new Entity(entity), (DetachMode)mode, parent == uint.MaxValue ? Entity.None : new Entity(parent));

    public static uint SpawnModel(uint index, uint generation)
    {
        Entity root = Skeletons.SpawnModel(new AssetHandle<Model>(index, generation));
        return root.Value;
    }
}
