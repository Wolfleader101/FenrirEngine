using Fenrir.Animation;
using Fenrir.Assets;
using Fenrir.Demo;
using Fenrir.DebugDraw;
using Fenrir.Input;
using Fenrir.Math;
using Fenrir.Renderer;
using Fenrir.Scripting;
using Fenrir.Skeleton;

namespace AnimationDemo.Scripts;

// BONE ATTACHMENTS (C#): SCENE 5'S ATTACHMENTS, MADE AND EDITED FROM C#
//
// The scene after "Playback (C#)". The Quaternius human walks; C# gives it, as BoneAttachments with BoneName.Set:
//   a blade on hand_r, an offset C# edits with the arrow keys;
//   a hat on Head;
//   a tag naming "no_such_bone", which stays BoneNotFound.
// Each is labelled with Skeletons.GetAttachmentState, and the screen text shows bone reads: the skeleton's bone count
// (SkeletonData), Head's world position (GetBoneWorldMatrix) and the generation (GetPoseGeneration).
//
// Two stage systems show C# inside the pose pipeline: a Fenrir.Skeleton.Modify system after the pose override nods the
// head through a PoseEdit, and a Fenrir.Skeleton.Late system marks hand_r where this frame's pose put it.
//
//   W          detaches the blade keeping where it is in the world (KeepWorld): it stays in the air.
//   K          detaches the hat keeping its local transform (KeepLocal).
//   R          attaches both again.
//   N          turns the C# nod on or off.
//   Left/Right moves the blade along hand_r's X; it stays on the bone.
//   C          the next clip: idle, walk, run.

public struct AttachmentsScene : IComponent
{
    public int Built; // 0 nothing yet, 1 the human spawned, 2 the props attached.
    public Entity Skeleton;
    public Entity Blade;
    public Entity Hat;
    public Entity Tag;
    public AssetHandle<Clip> Idle, Walk, Run;
    public int Clip;
    public bool Nod;
    public float BladeOffset;
    public int Head;
    public int Hand;
}

public static class AttachmentsCSharp
{
    // This scene's number in its CSharpScene, as C++ gives it (BoneAttachmentsCSharpScene in src/AnimationScenes.hpp).
    public const uint SceneId = 2;

    public static readonly Vector3 HatOffset = new(0, 0.2f, 0);
    public static readonly Vector3 TagPlace = new(1.2f, 1.0f, 0);

    // The scene's state, when it is active and built.
    public static bool TryGet(in SystemContext context, out AttachmentsScene state)
    {
        state = default;
        if (!CSharpScene.IsActive(SceneId, out _))
            return false;
        foreach (var (_, found) in context.Query<AttachmentsScene>())
        {
            state = found.Value;
            return state.Built == 2;
        }
        return false;
    }

    public static BoneAttachment AttachTo(Entity skeleton, string bone, Vector3 offset)
    {
        var attachment = new BoneAttachment { Skeleton = skeleton, Offset = new Transform { Position = offset } };
        attachment.Bone.Set(bone);
        return attachment;
    }

    public static Entity Prop(string text, AssetHandle<Mesh> mesh, Colour colour, Vector3 scale, Vector3 position = default)
    {
        Entity entity = Entity.Create();
        var name = new Name();
        name.SetText(text);
        entity.Add(name);
        entity.Add(new Transform { Position = position, Scale = scale });
        AssetHandle<Material> material = Materials.Create(Assets.Load<Shader>("builtin:lit"));
        Materials.SetColour(material, "baseColour", colour);
        entity.Add(new MeshRenderer { Mesh = mesh, Material = material });
        return entity;
    }

    public static string StateName(AttachmentState state) => state.ToString();
}

// Builds the scene once each time it is entered: the human, then the next frame the props on its bones.
[System(Phase.Update, Order = 100)]
public sealed class AttachmentsBuilder : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        if (!CSharpScene.IsActive(AttachmentsCSharp.SceneId, out _))
            return;
        foreach (var (_, state) in context.Query<AttachmentsScene>())
        {
            if (state.Value.Built == 1)
                Attach(in context, ref state.Value);
            return;
        }

        Entity root = Renderer.SpawnModel(Assets.Load<Model>("quaternius/rig-human.glb"));
        if (root == Entity.None)
        {
            context.Log.Error("Bone attachments (C#): quaternius/rig-human.glb did not spawn");
            return;
        }
        // Its components come when this system returns, so its place is recorded after them, as every change is.
        root.Add(new Transform { Position = new Vector3(-0.4f, 0, 0) });
        var scene = new AttachmentsScene
        {
            Built = 1,
            Idle = Assets.Load<Clip>("quaternius/idle_loop.glb#Clip0"),
            Walk = Assets.Load<Clip>("quaternius/walk_loop.glb#Clip0"),
            Run = Assets.Load<Clip>("quaternius/jog_fwd_loop.glb#Clip0"),
            Clip = 1,
            Nod = true,
            BladeOffset = 0.08f,
        };
        Entity holder = Entity.Create();
        var name = new Name();
        name.SetText("Bone attachments (C#) scene");
        holder.Add(name);
        holder.Add(scene);
    }

    private static void Attach(in SystemContext context, ref AttachmentsScene state)
    {
        foreach (var (entity, _) in context.Query<SkeletonPose>())
            state.Skeleton = entity;
        if (state.Skeleton == Entity.None)
        {
            context.Log.Error("Bone attachments (C#): the human spawned no skeleton entity");
            state.Built = 3;
            return;
        }
        state.Skeleton.Add(new Animator { Clip = state.Walk, FadeSeconds = 0.3f });
        state.Head = Skeletons.FindBone(state.Skeleton, "Head");
        state.Hand = Skeletons.FindBone(state.Skeleton, "hand_r");

        AssetHandle<Mesh> cube = Assets.Load<Mesh>("cube.obj#Mesh0");
        AssetHandle<Mesh> sphere = Assets.Load<Mesh>("sphere.obj#Mesh0");
        state.Blade = AttachmentsCSharp.Prop("Blade", cube, Colour.FromSrgb(0xC0C8D0FF), new Vector3(0.6f, 0.04f, 0.08f));
        state.Hat = AttachmentsCSharp.Prop("Hat", sphere, Colour.FromSrgb(0x3070E0FF), new Vector3(0.3f, 0.12f, 0.3f));
        state.Tag = AttachmentsCSharp.Prop("Tag", sphere, Colour.FromSrgb(0xE0B040FF), new Vector3(0.2f, 0.2f, 0.2f),
                                           AttachmentsCSharp.TagPlace);
        state.Blade.Add(AttachmentsCSharp.AttachTo(state.Skeleton, "hand_r", new Vector3(state.BladeOffset, 0.03f, 0)));
        state.Hat.Add(AttachmentsCSharp.AttachTo(state.Skeleton, "Head", AttachmentsCSharp.HatOffset));
        state.Tag.Add(AttachmentsCSharp.AttachTo(state.Skeleton, "no_such_bone", Vector3.Zero));
        state.Built = 2;
        context.Log.Info($"Bone attachments (C#): {SkeletonData.BoneCount(state.Skeleton.Get<SkeletonPose>().Value.Skeleton)} " +
                         "bones; a blade on hand_r, a hat on Head and a tag on no_such_bone");
    }
}

// The keys: detach, attach again, nod, move the blade, change the clip.
[System(Phase.Update, Order = 200)]
public sealed class AttachmentsKeys : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        if (!AttachmentsCSharp.TryGet(in context, out _))
            return;
        foreach (var (_, found) in context.Query<AttachmentsScene>())
        {
            Handle(in context, ref found.Value);
            return;
        }
    }

    // Gives the entity the attachment, or puts it back in place of the one it has.
    private static void Reattach(Entity entity, BoneAttachment attachment)
    {
        if (entity.Has<BoneAttachment>())
            entity.Get<BoneAttachment>().Value = attachment;
        else
            entity.Add(attachment);
    }

    private static void Handle(in SystemContext context, ref AttachmentsScene state)
    {
        if (Keyboard.JustPressed(Key.W))
        {
            DetachResult result = Skeletons.Detach(state.Blade, DetachMode.KeepWorld);
            context.Log.Info($"Bone attachments (C#): W detaches the blade keeping the world: {result}");
        }
        if (Keyboard.JustPressed(Key.K))
        {
            DetachResult result = Skeletons.Detach(state.Hat, DetachMode.KeepLocal);
            context.Log.Info($"Bone attachments (C#): K detaches the hat keeping its local transform: {result}");
        }
        if (Keyboard.JustPressed(Key.R))
        {
            Reattach(state.Blade, AttachmentsCSharp.AttachTo(state.Skeleton, "hand_r",
                                                             new Vector3(state.BladeOffset, 0.03f, 0)));
            Reattach(state.Hat, AttachmentsCSharp.AttachTo(state.Skeleton, "Head", AttachmentsCSharp.HatOffset));
            context.Log.Info("Bone attachments (C#): R attaches the blade and the hat again");
        }
        if (Keyboard.JustPressed(Key.N))
        {
            state.Nod = !state.Nod;
            context.Log.Info(state.Nod ? "Bone attachments (C#): N, the C# modifier nods the head"
                                       : "Bone attachments (C#): N, the nod is off");
        }
        if (Keyboard.JustPressed(Key.Left) || Keyboard.JustPressed(Key.Right))
        {
            state.BladeOffset += Keyboard.JustPressed(Key.Right) ? 0.05f : -0.05f;
            if (state.Blade.Has<BoneAttachment>())
                state.Blade.Get<BoneAttachment>().Value.Offset.Position = new Vector3(state.BladeOffset, 0.03f, 0);
            context.Log.Info($"Bone attachments (C#): the blade's offset along hand_r is {state.BladeOffset:0.00}");
        }
        if (Keyboard.JustPressed(Key.C))
        {
            state.Clip = (state.Clip + 1) % 3;
            state.Skeleton.Get<Animator>().Value.Clip = state.Clip switch
            {
                0 => state.Idle,
                1 => state.Walk,
                _ => state.Run,
            };
            context.Log.Info($"Bone attachments (C#): C plays clip {state.Clip}");
        }
    }
}

// The labels and the bone reads, each frame.
[System(Phase.Update, Order = 300)]
public sealed class AttachmentsShow : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        if (!AttachmentsCSharp.TryGet(in context, out AttachmentsScene state))
            return;
        Label(state.Blade, "Blade on hand_r");
        Label(state.Hat, "Hat on Head");
        Label(state.Tag, "Tag on no_such_bone");

        string head = Skeletons.GetBoneWorldMatrix(state.Skeleton, state.Head, out Matrix4x4 world)
                          ? $"({world.Column3.X:0.00}, {world.Column3.Y:0.00}, {world.Column3.Z:0.00})"
                          : "unread";
        int bones = SkeletonData.BoneCount(state.Skeleton.Get<SkeletonPose>().Value.Skeleton);
        DebugDraw.ScreenText(new Vector2(16, 120),
                             $"C#: {bones} bones, {Skeletons.GetPoseState(state.Skeleton)}, generation " +
                             $"{Skeletons.GetPoseGeneration(state.Skeleton)}; Head at {head}; nod " +
                             $"{(state.Nod ? "on" : "off")} (Fenrir.Skeleton.Modify)",
                             Colour.White);
    }

    private static void Label(Entity entity, string what)
    {
        AttachmentState state = Skeletons.GetAttachmentState(entity);
        Vector3 at = entity.Get<Transform>().Value.Position;
        DebugDraw.WorldText(at + new Vector3(0, 0.25f, 0), $"{what}: {AttachmentsCSharp.StateName(state)}",
                            state == AttachmentState.Attached ? Colour.FromSrgb(0x80FF80FF) : Colour.FromSrgb(0xFF9080FF),
                            new TextOptions { Size = 16 });
    }
}

// A pose modifier in C#, after the pose override (ModifierOrder.Override is 1000): nods the head by up to 0.35 rad
// about its local X, on top of whatever the clip gives it.
[StageSystem(PoseStages.Modify, Order = 1010)]
public sealed class HeadNod : ISystem
{
    private float clock;

    public void OnUpdate(in SystemContext context)
    {
        if (!AttachmentsCSharp.TryGet(in context, out AttachmentsScene state) || !state.Nod || state.Head < 0)
            return;
        clock += Time.DeltaTime;
        PoseEdit edit = Skeletons.Edit(state.Skeleton);
        if (!edit.Local(state.Head, out Transform head))
            return;
        float angle = 0.35f * MathF.Sin(clock * 3);
        var nod = new Quaternion(MathF.Sin(angle / 2), 0, 0, MathF.Cos(angle / 2));
        head.Rotation = Multiply(head.Rotation, nod);
        edit.Write(state.Head, new Target { Space = PoseSpace.Local, Trs = head, Channels = Channels.Rotation });
    }

    private static Quaternion Multiply(Quaternion a, Quaternion b) => new(
        a.W * b.X + a.X * b.W + a.Y * b.Z - a.Z * b.Y,
        a.W * b.Y - a.X * b.Z + a.Y * b.W + a.Z * b.X,
        a.W * b.Z + a.X * b.Y - a.Y * b.X + a.Z * b.W,
        a.W * b.W - a.X * b.X - a.Y * b.Y - a.Z * b.Z);
}

// After the pose is final: a marker on hand_r where this frame's pose put it, a frame ahead of an Update reader.
[StageSystem(PoseStages.Late)]
public sealed class HandMarker : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        if (!AttachmentsCSharp.TryGet(in context, out AttachmentsScene state) || state.Hand < 0)
            return;
        if (Skeletons.GetBoneWorldMatrix(state.Skeleton, state.Hand, out Matrix4x4 world))
            DebugDraw.Sphere(new Vector3(world.Column3.X, world.Column3.Y, world.Column3.Z), 0.04f,
                             Colour.FromSrgb(0xFF60C0FF));
    }
}
