using Fenrir.Ragdoll;
using Fenrir.Scripting;
using Fenrir.Skeleton;

namespace Fenrir.RagdollScriptFixture;

// Inside a Fenrir.* namespace, `Ragdoll` alone names the Fenrir.Ragdoll namespace: the alias names the component.
using Ragdoll = global::Fenrir.Ragdoll.Ragdoll;

// Ragdolls from a script: the tests call these through Iris, then play the changes back as the runner does after a
// system, and compare what C# did with what C++ holds. Entities cross as their ids, handles as an index into Handles.
public static class Ragdolls
{
    // The descriptions the tests asked for, by the index Describe returned.
    private static readonly List<RagdollDescriptionHandle> Handles = new();

    // The last ragdoll BuildFrom or DescribeAndBuild spawned.
    private static SpawnedRagdoll? built;

    // The test humanoid's bone map (FenrirRagdoll's Tests/RagdollTest.hpp, HumanoidMap), filled as a game fills one.
    public static HumanoidBones Map()
    {
        var bones = new HumanoidBones();
        bones.Hips.Set("pelvis");
        bones.Spine.Set("spine_01");
        bones.Chest.Set("spine_03");
        bones.Head.Set("Head");
        bones.LeftUpperArm.Set("upperarm_l");
        bones.LeftLowerArm.Set("lowerarm_l");
        bones.LeftHand.Set("hand_l");
        bones.RightUpperArm.Set("upperarm_r");
        bones.RightLowerArm.Set("lowerarm_r");
        bones.RightHand.Set("hand_r");
        bones.LeftUpperLeg.Set("thigh_l");
        bones.LeftLowerLeg.Set("calf_l");
        bones.LeftFoot.Set("foot_l");
        bones.RightUpperLeg.Set("thigh_r");
        bones.RightLowerLeg.Set("calf_r");
        bones.RightFoot.Set("foot_r");
        return bones;
    }

    // Describes the humanoid on the skeleton entity: the handle's index, or -1 for no description.
    public static int Describe(uint skeleton)
    {
        HumanoidBones bones = Map();
        return Keep(Ragdoll.DescribeHumanoid(new Entity(skeleton), in bones));
    }

    // Describes with a hips slot naming a bone the rig lacks: refused.
    public static int DescribeUnknownBone(uint skeleton)
    {
        HumanoidBones bones = Map();
        bones.Hips.Set("no_such_bone");
        return Keep(Ragdoll.DescribeHumanoid(new Entity(skeleton), in bones, 80.0f));
    }

    private static int Keep(RagdollDescriptionHandle handle)
    {
        if (handle.IsNone)
            return -1;
        Handles.Add(handle);
        return Handles.Count - 1;
    }

    public static int PartCount(int handle) => Ragdoll.PartCount(Handles[handle]);

    // Builds the kept description as the skeleton's ragdoll: how many bodies it spawned, or -1 when nothing was built.
    public static int BuildFrom(uint skeleton, int handle)
    {
        built = Ragdoll.Build(new Entity(skeleton), Handles[handle]);
        return built?.Bodies.Count ?? -1;
    }

    // Builds with no description at all.
    public static bool BuildNone(uint skeleton) => Ragdoll.Build(new Entity(skeleton), RagdollDescriptionHandle.None) is not null;

    // Builds into the caller's own buffers, one body short: refused, keeping the handle.
    public static bool BuildShort(uint skeleton, int handle)
    {
        int parts = Ragdoll.PartCount(Handles[handle]);
        Span<Entity> bodies = new Entity[parts - 1];
        Span<Entity> joints = new Entity[parts - 1];
        return Ragdoll.Build(new Entity(skeleton), Handles[handle], bodies, joints, out _);
    }

    // Describes and builds in one call, as a system would: the body count, or -1.
    public static int DescribeAndBuild(uint skeleton)
    {
        HumanoidBones bones = Map();
        RagdollDescriptionHandle handle = Ragdoll.DescribeHumanoid(new Entity(skeleton), in bones);
        built = Ragdoll.Build(new Entity(skeleton), handle);
        return built?.Bodies.Count ?? -1;
    }

    public static uint Parent() => built!.Parent.Value;

    public static uint Body(int index) => built!.Bodies[index].Value;

    public static int JointCount() => built!.Joints.Count;

    public static uint Joint(int index) => built!.Joints[index].Value;

    // Switches the ragdoll to simulating, as a drop does, through Ref<Ragdoll>.
    public static void Simulate(uint skeleton) => new Entity(skeleton).Get<Ragdoll>().Mode = RagdollMode.Simulating;

    // The glue's last findings, read in place through ref readonly.
    public static bool AnyAwake(uint skeleton)
    {
        ref readonly RagdollState state = ref new Entity(skeleton).Get<Ragdoll>().State;
        return state.AnyAwake;
    }

    public static int BodiesDriven(uint skeleton) => new Entity(skeleton).Get<Ragdoll>().State.Bodies;

    public static int BodyMode(uint body)
    {
        ref readonly RagdollBodyState state = ref new Entity(body).Get<RagdollBody>().State;
        return (int)state.Mode;
    }

    // Whether the body's bone, through the skeleton contract's BoneName, is `name`'s; the tests pass "pelvis" or not.
    public static bool BoneIsPelvis(uint body) => new Entity(body).Get<RagdollBody>().Bone.Get() == "pelvis";

    // Writes one body's own mode and strength scale, as a partial ragdoll does.
    public static void LimpBody(uint body)
    {
        var mapping = new Entity(body).Get<RagdollBody>();
        mapping.Mode = RagdollBodyMode.Simulating;
        mapping.StrengthScale = 0.5f;
    }

    public static bool Destroy(uint skeleton) => Ragdoll.Destroy(new Entity(skeleton));

    public static bool ClearOverrides(uint skeleton) => Ragdoll.ClearOverrides(new Entity(skeleton));
}
