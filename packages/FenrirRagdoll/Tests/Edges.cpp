// The edges of the glue (ragdolls spec, "the components" and "the glue each tick and frame"): every inactive and
// skipped rule warning once per transition and recovering by itself; a Ragdoll removed leaving its bodies skipped, its
// joints' motors off and its entries clearable; a skeleton asset rebuilt re-resolving names; glue-owned writes
// overwritten next tick; a paused scene changing nothing; and the scene, world and glue destroyed under a live ragdoll.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirRagdoll/Ragdolls.hpp"
#include "RagdollTest.hpp"

using Fenrir::Entity;
using Fenrir::RagdollMode;
using Fenrir::Skeletons;
using Fenrir::Math::Vec3;
using RagdollTest::BodyOf;
using RagdollTest::Character;
using RagdollTest::MotorMode;
using RagdollTest::Near;
using RagdollTest::Text;

namespace
{
    // Whether every resolved body of `character` is on its bone (bone × offset) now.
    bool AllOnBones(Character& character)
    {
        for (const Entity& body : character.spawned.bodies)
        {
            const Fenrir::Transform expected = RagdollTest::OnBone(character.test.level, body);
            if (!Near(Fenrir::GetWorldTransform(body).pos, expected.pos, 1e-4f))
            {
                return false;
            }
        }
        return true;
    }

    void SetMotorMode(Fenrir::Joint& joint, Fenrir::JointMotorMode mode)
    {
        if (joint.kind.Is<Fenrir::HingeJoint>())
        {
            joint.kind.As<Fenrir::HingeJoint>().motor.mode = mode;
        }
        else
        {
            joint.kind.As<Fenrir::SwingTwistJoint>().motor.mode = mode;
        }
    }

    // The humanoid with a new root bone above the pelvis: every bone's index one higher, its names unchanged.
    Fenrir::SkeletonData Rerooted()
    {
        Fenrir::SkeletonData data = RagdollTest::Humanoid();
        for (Fenrir::SkeletonBone& bone : data.bones)
        {
            bone.parent = bone.parent == Fenrir::NoBone ? 0 : bone.parent + 1;
        }
        data.bones.insert(data.bones.begin(), RagdollTest::Bone("root", Fenrir::NoBone, Vec3(0.0f)));
        return data;
    }

    // What the glue and physics may change about a ragdoll, as plain values, to compare two moments.
    struct Snapshot
    {
        std::vector<Fenrir::Transform> bodies;
        std::vector<Vec3> velocities;
        std::vector<Fenrir::MotionType> motionTypes;
        std::vector<Fenrir::Math::Quat> targets;
        std::vector<Fenrir::JointMotorMode> motorModes;
        std::vector<std::optional<Fenrir::Target>> overrides;
        Fenrir::Transform skeleton;
        Fenrir::RagdollState state;

        explicit Snapshot(Character& character)
            : skeleton(Fenrir::GetWorldTransform(character.skeleton)), state(character.Ragdoll().state)
        {
            for (const Entity& body : character.spawned.bodies)
            {
                const Fenrir::RigidBody& rigid = body.GetComponent<Fenrir::RigidBody>();
                bodies.push_back(Fenrir::GetWorldTransform(body));
                velocities.push_back(rigid.linearVelocity);
                velocities.push_back(rigid.angularVelocity);
                motionTypes.push_back(rigid.motionType);
                overrides.push_back(character.OverrideOf(body));
            }
            for (const Entity& entity : character.spawned.joints)
            {
                const Fenrir::Joint& joint = entity.GetComponent<Fenrir::Joint>();
                motorModes.push_back(MotorMode(joint));
                targets.push_back(
                    joint.kind.Is<Fenrir::SwingTwistJoint>()
                        ? joint.kind.As<Fenrir::SwingTwistJoint>().motor.targetRotation
                        : Fenrir::Math::AngleAxis(joint.kind.As<Fenrir::HingeJoint>().motor.targetPosition,
                                                  Vec3(1.0f, 0.0f, 0.0f)));
            }
        }

        // Every value equal to `other`'s, exactly; the first that differs is named in `why`.
        bool Same(const Snapshot& other, std::string& why) const
        {
            for (std::size_t i = 0; i < bodies.size(); ++i)
            {
                if (bodies[i].pos != other.bodies[i].pos || bodies[i].rot != other.bodies[i].rot)
                {
                    why = "body " + std::to_string(i) + "'s pose";
                    return false;
                }
                if (motionTypes[i] != other.motionTypes[i])
                {
                    why = "body " + std::to_string(i) + "'s motion type";
                    return false;
                }
                if (overrides[i].has_value() != other.overrides[i].has_value() ||
                    (overrides[i] && (overrides[i]->trs.pos != other.overrides[i]->trs.pos ||
                                      overrides[i]->trs.rot != other.overrides[i]->trs.rot)))
                {
                    why = "body " + std::to_string(i) + "'s bone override";
                    return false;
                }
            }
            for (std::size_t i = 0; i < velocities.size(); ++i)
            {
                if (velocities[i] != other.velocities[i])
                {
                    why = "a body's velocity";
                    return false;
                }
            }
            for (std::size_t i = 0; i < targets.size(); ++i)
            {
                if (targets[i] != other.targets[i] || motorModes[i] != other.motorModes[i])
                {
                    why = "joint " + std::to_string(i) + "'s motor";
                    return false;
                }
            }
            if (skeleton.pos != other.skeleton.pos || skeleton.rot != other.skeleton.rot)
            {
                why = "the skeleton entity's pose";
                return false;
            }
            if (state.anyAwake != other.state.anyAwake || state.bodies != other.state.bodies ||
                state.inactive != other.state.inactive)
            {
                why = "the ragdoll's state";
                return false;
            }
            return true;
        }
    };
} // namespace

TEST_CASE("A ragdoll whose skeleton stops being ready warns once each time, and resumes by itself", "[ragdoll][edges]")
{
    Character character(RagdollTest::StepPosesOnly(), 1.0f);
    character.test.Run(2);
    const Fenrir::AssetHandle<Fenrir::Skeleton> handle =
        character.skeleton.GetComponent<Fenrir::SkeletonPose>().skeleton;

    character.skeleton.GetComponent<Fenrir::SkeletonPose>().skeleton = {};
    character.test.Run(5);
    {
        INFO("not ready: inactive, one warning over five frames");
        REQUIRE(character.Ragdoll().state.inactive);
        REQUIRE(character.test.Warnings("is inactive") == 1);
    }

    character.skeleton.GetComponent<Fenrir::SkeletonPose>().skeleton = handle;
    character.test.Run(3);
    {
        INFO("ready again: active, its bodies on their bones, no new ragdoll warning");
        REQUIRE_FALSE(character.Ragdoll().state.inactive);
        REQUIRE(character.Ragdoll().state.bodies == 16);
        REQUIRE(AllOnBones(character));
        REQUIRE(character.test.Warnings("Ragdoll") == 1);
    }

    character.skeleton.GetComponent<Fenrir::SkeletonPose>().skeleton = {};
    character.test.Run(3);
    INFO("not ready a second time: a second warning");
    REQUIRE(character.test.Warnings("is inactive") == 2);
}

TEST_CASE("A ragdoll whose Ragdoll is removed while inactive warns again when it comes back inactive",
          "[ragdoll][edges]")
{
    Character character;
    character.skeleton.GetComponent<Fenrir::SkeletonPose>().timing = Fenrir::PoseTiming::Frame;
    character.test.Run(3);
    {
        INFO("frame-posed: one warning");
        REQUIRE(character.test.Warnings("PoseTiming::Tick") == 1);
    }

    const Fenrir::Ragdoll ragdoll = character.Ragdoll();
    character.skeleton.RemoveComponent<Fenrir::Ragdoll>();
    character.test.Run(2);
    character.skeleton.AddComponent<Fenrir::Ragdoll>(ragdoll);
    character.test.Run(3);
    INFO("a new Ragdoll on the frame-posed skeleton is a new transition: one more warning");
    REQUIRE(character.test.Warnings("PoseTiming::Tick") == 2);
}

TEST_CASE("A body without a RigidBody is skipped, warning once each time, and follows again once it has one",
          "[ragdoll][edges]")
{
    Character character(RagdollTest::StepPosesOnly(), 1.0f);
    character.test.Run(2);
    Entity hand = BodyOf(character.spawned, "hand_l");
    const Fenrir::RigidBody rigid = hand.GetComponent<Fenrir::RigidBody>();

    hand.RemoveComponent<Fenrir::RigidBody>();
    character.test.Run(5);
    {
        INFO("one warning over five frames, NoBone, fifteen bodies driven");
        REQUIRE(character.test.Warnings("has no RigidBody") == 1);
        REQUIRE(hand.GetComponent<Fenrir::RagdollBody>().state.bone == Fenrir::NoBone);
        REQUIRE(character.Ragdoll().state.bodies == 15);
    }

    hand.AddComponent<Fenrir::RigidBody>(rigid);
    character.test.Run(2);
    {
        INFO("a RigidBody again: resolved and on its bone, sixteen driven");
        REQUIRE(hand.GetComponent<Fenrir::RagdollBody>().state.bone == character.Bone("hand_l"));
        REQUIRE(character.Ragdoll().state.bodies == 16);
        REQUIRE(AllOnBones(character));
    }

    hand.RemoveComponent<Fenrir::RigidBody>();
    character.test.Run(2);
    INFO("removed again: warned again");
    REQUIRE(character.test.Warnings("has no RigidBody") == 2);
}

TEST_CASE("A body whose ragdoll is no Ragdoll entity of its scene is skipped, warning once, and follows when it is",
          "[ragdoll][edges]")
{
    Character character(RagdollTest::StepPosesOnly(), 1.0f);
    character.test.Run(2);
    const Entity hand = BodyOf(character.spawned, "hand_l");
    Fenrir::RagdollBody& mapping = hand.GetComponent<Fenrir::RagdollBody>();

    SECTION("an entity of the scene with no Ragdoll")
    {
        mapping.ragdoll = character.spawned.parent;
    }
    SECTION("a Ragdoll entity of another scene")
    {
        Fenrir::Scene& other = character.test.app.CreateScene("Other");
        Entity elsewhere = other.GetEntityList().CreateEntity();
        elsewhere.AddComponent<Fenrir::Ragdoll>();
        mapping.ragdoll = elsewhere;
    }
    SECTION("a destroyed entity")
    {
        Entity gone = character.test.Entities().CreateEntity();
        gone.AddComponent<Fenrir::Ragdoll>();
        character.test.Entities().DestroyEntity(gone);
        mapping.ragdoll = gone;
    }
    character.test.Run(5);
    {
        INFO("one warning over five frames, NoBone, fifteen bodies driven");
        REQUIRE(character.test.Warnings("is skipped: its ragdoll is not a live entity") == 1);
        REQUIRE(hand.GetComponent<Fenrir::RagdollBody>().state.bone == Fenrir::NoBone);
        REQUIRE(character.Ragdoll().state.bodies == 15);
    }

    hand.GetComponent<Fenrir::RagdollBody>().ragdoll = character.skeleton;
    character.test.Run(2);
    INFO("its ragdoll again: resolved and on its bone, no new warning");
    REQUIRE(hand.GetComponent<Fenrir::RagdollBody>().state.bone == character.Bone("hand_l"));
    REQUIRE(AllOnBones(character));
    REQUIRE(character.test.warnings->size() == 1);
}

TEST_CASE("A body renamed away twice is warned about twice, once each time", "[ragdoll][edges]")
{
    Character character;
    character.test.Run(1);
    const Entity hand = BodyOf(character.spawned, "hand_l");
    for (int time = 0; time < 2; ++time)
    {
        hand.GetComponent<Fenrir::RagdollBody>().bone = RagdollTest::NameOf("hand_left");
        character.test.Run(4);
        hand.GetComponent<Fenrir::RagdollBody>().bone = RagdollTest::NameOf("hand_l");
        character.test.Run(2);
        INFO("time " << time << ": named right again, it resolves");
        REQUIRE(hand.GetComponent<Fenrir::RagdollBody>().state.bone == character.Bone("hand_l"));
    }
    INFO("two transitions, two warnings");
    REQUIRE(character.test.Warnings("hand_left") == 2);
}

TEST_CASE("Removing Ragdoll turns its joints' motors off once and leaves them to game code",
          "[ragdoll][edges][lifetime]")
{
    Character character;
    character.Ragdoll().mode = RagdollMode::Powered;
    character.test.Run(10);
    for (const Entity& joint : character.spawned.joints)
    {
        INFO("powered: every joint driven");
        REQUIRE(MotorMode(joint.GetComponent<Fenrir::Joint>()) == Fenrir::JointMotorMode::Position);
    }

    character.skeleton.RemoveComponent<Fenrir::Ragdoll>();
    character.test.Run(1);
    for (const Entity& joint : character.spawned.joints)
    {
        INFO("the joint to " << Text(joint.GetComponent<Fenrir::Joint>().bodyB.GetComponent<Fenrir::RagdollBody>().bone)
                             << " is off: a removed ragdoll drives nothing");
        REQUIRE(MotorMode(joint.GetComponent<Fenrir::Joint>()) == Fenrir::JointMotorMode::Off);
    }
    {
        INFO("the bodies stay dynamic, where they are");
        REQUIRE(character.spawned.bodies.front().GetComponent<Fenrir::RigidBody>().motionType ==
                Fenrir::MotionType::Dynamic);
    }

    Fenrir::Joint& elbow = *RagdollTest::JointTo(character.spawned, BodyOf(character.spawned, "lowerarm_l"));
    SetMotorMode(elbow, Fenrir::JointMotorMode::Position);
    character.test.Run(5);
    INFO("once off, the joints are game code's: a motor it turns on stays on");
    REQUIRE(MotorMode(elbow) == Fenrir::JointMotorMode::Position);
}

TEST_CASE("Ragdolls::ClearOverrides clears a removed Ragdoll's entries", "[ragdoll][edges][lifetime]")
{
    Character character;
    character.Ragdoll().mode = RagdollMode::Simulating;
    character.test.Run(10);
    // A body destroyed while simulating leaves its entry, which only the glue's record still names.
    const Entity hand = BodyOf(character.spawned, "hand_l");
    const std::int32_t handBone = character.BoneOf(hand);
    for (const Entity& joint : character.spawned.joints)
    {
        if (joint.GetComponent<Fenrir::Joint>().bodyB == hand)
        {
            character.test.Entities().DestroyEntity(joint);
        }
    }
    character.test.Entities().DestroyEntity(hand);
    character.test.Run(2);
    character.skeleton.RemoveComponent<Fenrir::Ragdoll>();
    character.test.Run(3);
    {
        INFO("the entries stay after the Ragdoll goes");
        REQUIRE(character.OverrideOf(character.spawned.bodies.front()).has_value());
        REQUIRE(Skeletons::GetOverride(character.test.level, character.skeleton, handBone).has_value());
    }

    {
        INFO("its bodies still name the skeleton, so there is something to clear");
        REQUIRE(Fenrir::Ragdolls::ClearOverrides(character.test.level, character.skeleton));
    }
    for (const Entity& body : character.spawned.bodies)
    {
        if (body == hand)
        {
            continue;
        }
        INFO(Text(body.GetComponent<Fenrir::RagdollBody>().bone) << ": its entry went, its weight stayed");
        REQUIRE_FALSE(character.OverrideOf(body).has_value());
        REQUIRE(character.WeightOf(body) == 1.0f);
    }
    INFO("and the destroyed hand's entry went too");
    REQUIRE_FALSE(Skeletons::GetOverride(character.test.level, character.skeleton, handBone).has_value());
}

TEST_CASE("A Ragdoll put back on the skeleton drives its bodies again, with no new warning",
          "[ragdoll][edges][lifetime]")
{
    Character character(RagdollTest::StepPosesOnly(), 1.0f);
    character.test.Run(2);
    const Fenrir::Ragdoll ragdoll = character.Ragdoll();
    character.skeleton.RemoveComponent<Fenrir::Ragdoll>();
    character.test.Run(3);
    {
        INFO("removed: each body warned about once");
        REQUIRE(character.test.Warnings("is skipped") == character.spawned.bodies.size());
    }

    character.skeleton.AddComponent<Fenrir::Ragdoll>(ragdoll);
    character.test.Run(2);
    INFO("every body resolved and on its bone, no warning beyond the first ones");
    REQUIRE(character.Ragdoll().state.bodies == 16);
    REQUIRE(AllOnBones(character));
    REQUIRE(character.test.warnings->size() == character.spawned.bodies.size());
}

TEST_CASE("A skeleton asset rebuilt with its bones reordered re-resolves every body by name", "[ragdoll][edges]")
{
    Character character(RagdollTest::StepPosesOnly(), 1.0f);
    character.test.Run(2);
    const auto handle = character.skeleton.GetComponent<Fenrir::SkeletonPose>().skeleton;
    std::vector<std::int32_t> before;
    for (const Entity& body : character.spawned.bodies)
    {
        before.push_back(body.GetComponent<Fenrir::RagdollBody>().state.bone);
    }
    {
        INFO("the store takes the rebuilt skeleton");
        REQUIRE(character.test.assets.Store().Replace(handle, Rerooted()));
    }
    character.test.Run(3);

    for (std::size_t i = 0; i < character.spawned.bodies.size(); ++i)
    {
        const Fenrir::RagdollBody& mapping = character.spawned.bodies[i].GetComponent<Fenrir::RagdollBody>();
        INFO(Text(mapping.bone) << " resolves to its new index, one above its old");
        REQUIRE(mapping.state.bone == character.test.DataOf(character.skeleton).FindBone(Text(mapping.bone)));
        REQUIRE(mapping.state.bone == before[i] + 1);
    }
    INFO("and every body is on its bone in the rebuilt skeleton");
    REQUIRE(AllOnBones(character));
}

TEST_CASE("Game code's writes to glue-owned fields are overwritten next tick", "[ragdoll][edges]")
{
    Character character;
    character.test.Run(3);
    const Entity chest = BodyOf(character.spawned, "spine_03");
    const Entity hand = BodyOf(character.spawned, "hand_l");

    SECTION("a following body's motion type and velocities")
    {
        Fenrir::RigidBody& rigid = chest.GetComponent<Fenrir::RigidBody>();
        rigid.motionType = Fenrir::MotionType::Dynamic;
        rigid.linearVelocity = Vec3(5.0f, 0.0f, 0.0f);
        rigid.angularVelocity = Vec3(0.0f, 5.0f, 0.0f);
        character.test.Run(1);
        INFO("kinematic again, still (the pose does not move), on its bone");
        REQUIRE(rigid.motionType == Fenrir::MotionType::Kinematic);
        REQUIRE(Near(rigid.linearVelocity, Vec3(0.0f)));
        REQUIRE(Near(rigid.angularVelocity, Vec3(0.0f)));
        REQUIRE(AllOnBones(character));
    }
    SECTION("a simulating body's motion type")
    {
        character.Ragdoll().mode = RagdollMode::Simulating;
        character.test.Run(2);
        chest.GetComponent<Fenrir::RigidBody>().motionType = Fenrir::MotionType::Kinematic;
        character.test.Run(1);
        INFO("dynamic again");
        REQUIRE(chest.GetComponent<Fenrir::RigidBody>().motionType == Fenrir::MotionType::Dynamic);
    }
    SECTION("a powered joint's motor mode and target")
    {
        character.Ragdoll().mode = RagdollMode::Powered;
        character.test.Run(2);
        Fenrir::Joint& wrist = *RagdollTest::JointTo(character.spawned, hand);
        auto& motor = wrist.kind.As<Fenrir::SwingTwistJoint>().motor;
        const Fenrir::Math::Quat target = motor.targetRotation;
        motor.mode = Fenrir::JointMotorMode::Off;
        motor.targetRotation = Fenrir::Math::AngleAxis(1.0f, Vec3(0.0f, 0.0f, 1.0f));
        character.test.Run(1);
        INFO("Position again, aimed at the base pose again");
        REQUIRE(motor.mode == Fenrir::JointMotorMode::Position);
        REQUIRE(Near(motor.targetRotation, target));
    }
    SECTION("the skeleton entity's Transform while it follows the root")
    {
        character.Ragdoll().mode = RagdollMode::Simulating;
        character.test.Run(2);
        const Vec3 under = Fenrir::GetWorldTransform(character.skeleton).pos;
        character.skeleton.GetComponent<Fenrir::Transform>().pos = Vec3(9.0f, under.y, 9.0f);
        character.test.Run(1);
        INFO("back under the root body");
        REQUIRE(Near(Fenrir::GetWorldTransform(character.skeleton).pos, under, 0.05f));
    }
}

TEST_CASE("A paused scene's ragdoll changes nothing, in every mode", "[ragdoll][edges][scenes]")
{
    Character character(RagdollTest::StepPosesOnly(), 1.0f);
    character.test.Run(5);
    // Powered, with a simulating left arm and following legs.
    character.Ragdoll().mode = RagdollMode::Powered;
    RagdollTest::SetModes(character.spawned, {"upperarm_l", "lowerarm_l", "hand_l"},
                          Fenrir::RagdollBodyMode::Simulating);
    RagdollTest::SetModes(character.spawned, {"thigh_l", "calf_l", "foot_l", "thigh_r", "calf_r", "foot_r"},
                          Fenrir::RagdollBodyMode::Following);
    character.test.Run(10);
    const Snapshot before(character);

    character.test.app.CreateScene("Other");
    character.test.app.ChangeActiveScene("Other");
    character.test.Run(30);
    character.test.app.ChangeActiveScene("Level");

    std::string why;
    const bool same = Snapshot(character).Same(before, why);
    INFO("thirty frames paused: " << (same ? "nothing" : why) << " changed");
    REQUIRE(same);
    REQUIRE(character.test.warnings->empty());
}

TEST_CASE("Destroying the scene of a live ragdoll is clean, and the app runs on", "[ragdoll][edges][lifetime]")
{
    Character character;
    character.Ragdoll().mode = RagdollMode::Simulating;
    character.test.Run(10);

    Fenrir::Scene& other = character.test.app.CreateScene("Other");
    character.test.app.ChangeActiveScene("Other");
    character.test.app.DestroyScene("Level");
    // The other scene with physics in it, so its world steps beside the gone one's.
    Entity box = other.GetEntityList().CreateEntity();
    box.AddComponent<Fenrir::RigidBody>();
    box.AddComponent<Fenrir::Collider>(Fenrir::Collider{Fenrir::BoxShape{Vec3(0.5f)}});
    character.test.Run(10);
    INFO("no warning or error");
    REQUIRE(character.test.warnings->empty());
    REQUIRE(character.test.errors->empty());
}

TEST_CASE("Ending physics under a live ragdoll is clean, and the bodies still follow", "[ragdoll][edges][lifetime]")
{
    Character character(RagdollTest::StepPosesOnly(), 1.0f);
    character.Ragdoll().mode = RagdollMode::Powered;
    character.test.Run(10);
    character.Ragdoll().mode = RagdollMode::Following;

    {
        const Fenrir::Physics gone = std::move(character.test.physics);
    }
    REQUIRE(Fenrir::PhysicsWorld::Of(character.test.level) == nullptr);
    character.test.Run(5);
    {
        INFO("the glue still writes the components: every body on its bone, all sixteen driven, asleep with no world");
        REQUIRE(AllOnBones(character));
        REQUIRE(character.Ragdoll().state.bodies == 16);
        REQUIRE_FALSE(character.Ragdoll().state.anyAwake);
    }
    INFO("no warning or error");
    REQUIRE(character.test.warnings->empty());
    REQUIRE(character.test.errors->empty());
}

TEST_CASE("Ending the glue leaves every ragdoll as it was, and its scene can be destroyed",
          "[ragdoll][edges][lifetime]")
{
    Character character;
    character.Ragdoll().mode = RagdollMode::Simulating;
    character.test.Run(10);
    {
        const Fenrir::Ragdolls gone = std::move(character.test.ragdolls);
    }
    const Fenrir::Target entry = *character.OverrideOf(character.Ragdoll().rootBody);
    character.test.Run(10);
    {
        INFO("no entry is written once the glue is gone");
        REQUIRE(character.OverrideOf(character.Ragdoll().rootBody)->trs.pos == entry.trs.pos);
    }

    character.test.app.CreateScene("Other");
    character.test.app.ChangeActiveScene("Other");
    character.test.app.DestroyScene("Level");
    character.test.Run(3);
    INFO("no warning or error");
    REQUIRE(character.test.warnings->empty());
    REQUIRE(character.test.errors->empty());
}
