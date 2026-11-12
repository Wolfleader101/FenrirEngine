// Simulating (ADR-0038, ADR-0054): a ragdoll going limp without moving, keeping its following velocity, its bodies
// falling and settling; each simulating body's bone written as a world pose override through offset⁻¹, at gameplay's
// weights, never the glue's; a partial ragdoll's arm hanging from its following shoulder; the skeleton entity following
// the root body horizontally and in yaw; getting up, the entries held until every weight reads 0 and cleared at once
// by Ragdolls::ClearOverrides; and the lifetime and sleep rules.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirECS/WorldTransform.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirRagdoll/Ragdolls.hpp"
#include "RagdollTest.hpp"

using Fenrir::Entity;
using Fenrir::RagdollMode;
using Fenrir::Skeletons;
using Fenrir::SpawnedRagdoll;
using Fenrir::Math::Quat;
using Fenrir::Math::Vec3;
using RagdollTest::BodyOf;
using RagdollTest::Near;
using RagdollTest::Text;

namespace
{
    // A humanoid standing on the ground, optionally walking along +X, its ragdoll built and following.
    struct Character
    {
        RagdollTest::RagdollApp test;
        Entity skeleton;
        SpawnedRagdoll spawned;

        explicit Character(const Fenrir::PhysicsConfig& config = RagdollTest::StepPosesOnly(), float walk = 0.0f,
                           const Fenrir::Transform& place = {})
            : test(config)
        {
            test.AddGround();
            skeleton = test.AddSkeleton(RagdollTest::Humanoid());
            skeleton.GetComponent<Fenrir::Transform>() = place;
            if (walk != 0.0f)
            {
                RagdollTest::AddWalk(test, walk);
            }
            spawned = Fenrir::BuildRagdoll(test.app, test.level, skeleton,
                                           Fenrir::DescribeHumanoid(test.DataOf(skeleton), RagdollTest::HumanoidMap()));
            REQUIRE(spawned.error.empty());
        }

        Fenrir::Ragdoll& Ragdoll()
        {
            return skeleton.GetComponent<Fenrir::Ragdoll>();
        }

        std::int32_t Bone(std::string_view name)
        {
            return Skeletons::FindBone(test.level, skeleton, name);
        }

        std::int32_t BoneOf(const Entity& body)
        {
            return Bone(Text(body.GetComponent<Fenrir::RagdollBody>().bone));
        }

        std::optional<Fenrir::Target> OverrideOf(const Entity& body)
        {
            return Skeletons::GetOverride(test.level, skeleton, BoneOf(body));
        }

        // Runs until the ragdoll's bodies are all asleep, at least a second and at most `frames` frames; whether
        // they slept.
        bool RunUntilAsleep(int frames)
        {
            test.Run(60);
            for (int i = 60; i < frames; ++i)
            {
                test.Run(1);
                if (!Ragdoll().state.anyAwake)
                {
                    return true;
                }
            }
            return false;
        }
    };

    Fenrir::PhysicsConfig Weightless()
    {
        Fenrir::PhysicsConfig config = RagdollTest::StepPosesOnly();
        config.gravity = Vec3(0.0f);
        return config;
    }

    Fenrir::Transform Decomposed(const Fenrir::Math::Mat4& matrix)
    {
        Fenrir::Transform out;
        Fenrir::Math::Decompose(matrix, out.pos, out.rot, out.scale);
        return out;
    }

    // Where the render pose has `bone` now, in world space.
    Fenrir::Transform RenderBone(Character& character, std::int32_t bone)
    {
        return Decomposed(*Skeletons::GetBoneWorldMatrix(character.test.level, character.skeleton, bone));
    }

    // The rotation `rotation` turns +Z to, flattened onto the ground, as a turn about +Y.
    Quat Heading(const Quat& rotation)
    {
        const Vec3 forward = Fenrir::Math::Rotate(rotation, Vec3(0.0f, 0.0f, 1.0f));
        return Fenrir::Math::AngleAxis(std::atan2(forward.x, forward.z), Vec3(0.0f, 1.0f, 0.0f));
    }

    bool Upright(const Quat& rotation)
    {
        return Near(Fenrir::Math::Rotate(rotation, Vec3(0.0f, 1.0f, 0.0f)), Vec3(0.0f, 1.0f, 0.0f), 1e-4f);
    }

    const Fenrir::Joint* JointTo(const SpawnedRagdoll& spawned, const Entity& body)
    {
        for (const Entity& joint : spawned.joints)
        {
            if (joint.GetComponent<Fenrir::Joint>().bodyB == body)
            {
                return &joint.GetComponent<Fenrir::Joint>();
            }
        }
        return nullptr;
    }

    // How far apart a joint's two anchors are: where each body puts it.
    float AnchorGap(const Fenrir::Joint& joint)
    {
        const Vec3 a = Vec3(Fenrir::GetWorldMatrix(joint.bodyA) * Fenrir::Math::Vec4(joint.frameA.position, 1.0f));
        const Vec3 b = Vec3(Fenrir::GetWorldMatrix(joint.bodyB) * Fenrir::Math::Vec4(joint.frameB.position, 1.0f));
        return Fenrir::Math::Magnitude(a - b);
    }
} // namespace

TEST_CASE("Simulating makes every body dynamic where it is, keeping its following velocity, and the bodies fall and "
          "settle on the floor",
          "[ragdoll][simulate]")
{
    constexpr float Walk = 1.0f;
    Character character(RagdollTest::StepPosesOnly(), Walk);
    character.test.Run(30);
    std::vector<Vec3> before;
    for (const Entity& body : character.spawned.bodies)
    {
        before.push_back(Fenrir::GetWorldTransform(body).pos);
    }

    character.Ragdoll().mode = RagdollMode::Simulating;
    character.test.Run(1);

    for (std::size_t i = 0; i < character.spawned.bodies.size(); ++i)
    {
        const Entity& body = character.spawned.bodies[i];
        INFO(Text(body.GetComponent<Fenrir::RagdollBody>().bone));
        INFO("dynamic, its effective mode simulating");
        REQUIRE(body.GetComponent<Fenrir::RigidBody>().motionType == Fenrir::MotionType::Dynamic);
        REQUIRE(body.GetComponent<Fenrir::RagdollBody>().state.mode == RagdollMode::Simulating);
        const Vec3 moved = Fenrir::GetWorldTransform(body).pos - before[i];
        INFO("one step on from where it was, at the walk's speed: (" << moved.x << ", " << moved.y << ", " << moved.z
                                                                     << ")");
        REQUIRE(Near(moved.x, Walk * static_cast<float>(RagdollTest::Tick), 2e-3f));
        REQUIRE(Near(moved.z, 0.0f, 2e-3f));
        INFO("and gravity's one step down, g dt²");
        REQUIRE(Near(moved.y, -9.81f * static_cast<float>(RagdollTest::Tick * RagdollTest::Tick), 1e-3f));
        REQUIRE(Near(body.GetComponent<Fenrir::RigidBody>().linearVelocity.x, Walk, 0.1f));
    }

    INFO("the bodies fall, tumble forward and fall asleep on the floor");
    REQUIRE(character.RunUntilAsleep(1200));
    for (std::size_t i = 0; i < character.spawned.bodies.size(); ++i)
    {
        const Vec3 at = Fenrir::GetWorldTransform(character.spawned.bodies[i]).pos;
        INFO(Text(character.spawned.bodies[i].GetComponent<Fenrir::RagdollBody>().bone)
             << " at (" << at.x << ", " << at.y << ", " << at.z << ")");
        REQUIRE(at.y > 0.0f);
        REQUIRE(at.y < 0.6f);
    }
    REQUIRE(Fenrir::GetWorldTransform(character.Ragdoll().rootBody).pos.x > before.front().x + 0.1f);
    REQUIRE(character.test.warnings->empty());
}

TEST_CASE("Each simulating body puts its bone, in the render pose, where the body is through offset⁻¹, at gameplay's "
          "weights",
          "[ragdoll][simulate][override]")
{
    // Interpolation on and a frame of a tick and a half: the bones follow the bodies as they are drawn.
    Fenrir::PhysicsConfig config = RagdollTest::StepPosesOnly();
    config.interpolate = true;
    Character character(config);
    const std::int32_t head = character.Bone("Head");
    REQUIRE(Skeletons::SetOverrideWeight(character.test.level, character.skeleton, head, 0.3f));
    character.test.Run(5);
    character.Ragdoll().mode = RagdollMode::Simulating;

    for (int frame = 0; frame < 20; ++frame)
    {
        character.test.Run(1, 1.5 * RagdollTest::Tick);
        for (const Entity& body : character.spawned.bodies)
        {
            const std::int32_t bone = character.BoneOf(body);
            if (bone == head)
            {
                continue; // at 0.3, checked below
            }
            const Fenrir::Transform expected = RagdollTest::BoneFromBody(body);
            const Fenrir::Transform actual = RenderBone(character, bone);
            INFO("frame " << frame << ", " << Text(body.GetComponent<Fenrir::RagdollBody>().bone));
            REQUIRE(Near(actual.pos, expected.pos, 1e-4f));
            REQUIRE(Near(actual.rot, expected.rot, 1e-4f));
            const std::optional<Fenrir::Target> entry = character.OverrideOf(body);
            REQUIRE(entry.has_value());
            REQUIRE(entry->space == Fenrir::PoseSpace::World);
            REQUIRE(entry->channels == (Fenrir::Channels::Translation | Fenrir::Channels::Rotation));
            REQUIRE(entry->weight == 1.0f);
        }
    }

    // A subtree at 0.5: the lower arm halfway, in its parent's space, from its animated local to the body's.
    const std::int32_t lowerArm = character.Bone("lowerarm_l");
    REQUIRE(Skeletons::SetOverrideWeight(character.test.level, character.skeleton, lowerArm, 0.5f,
                                         Fenrir::OverrideScope::BoneSubtree));
    character.test.Run(1, 1.5 * RagdollTest::Tick);
    const Fenrir::Transform target = RagdollTest::BoneFromBody(BodyOf(character.spawned, "lowerarm_l"));
    const std::int32_t upperArm = character.Bone("upperarm_l");
    const Fenrir::Math::Mat4 parentWorld =
        *Skeletons::GetBoneWorldMatrix(character.test.level, character.skeleton, upperArm);
    const Fenrir::Transform wanted = Decomposed(Fenrir::Math::Inverse(parentWorld) * Fenrir::TransformToMat4(target));
    const Fenrir::Transform animated = *Skeletons::GetBaseLocal(character.test.level, character.skeleton, lowerArm);
    const Fenrir::Transform blended = *Skeletons::GetLocal(character.test.level, character.skeleton, lowerArm);
    INFO("the lower arm's local halfway between its animated pose and its body's");
    REQUIRE(Near(blended.pos, (animated.pos + wanted.pos) * 0.5f, 1e-4f));
    REQUIRE(Near(blended.rot, Fenrir::Math::Slerp(animated.rot, wanted.rot, 0.5f), 1e-4f));

    INFO("the glue changed no weight");
    for (const Entity& body : character.spawned.bodies)
    {
        const std::int32_t bone = character.BoneOf(body);
        const bool inArm = bone == lowerArm || bone == character.Bone("hand_l");
        const float expected = bone == head ? 0.3f : (inArm ? 0.5f : 1.0f);
        REQUIRE(Skeletons::GetOverrideWeight(character.test.level, character.skeleton, bone) == expected);
    }
    REQUIRE(character.test.warnings->empty());
}

TEST_CASE("A simulating arm under a following shoulder hangs from its joint without stretching, and only its bones get "
          "overrides",
          "[ragdoll][simulate][partial]")
{
    Character character(RagdollTest::StepPosesOnly(), 1.0f);
    const std::vector<std::string_view> arm = {"upperarm_l", "lowerarm_l", "hand_l"};
    for (const std::string_view bone : arm)
    {
        BodyOf(character.spawned, bone).GetComponent<Fenrir::RagdollBody>().mode = Fenrir::RagdollBodyMode::Simulating;
    }
    const Fenrir::Transform place = Fenrir::GetWorldTransform(character.skeleton);
    character.test.Run(60);

    for (const Entity& body : character.spawned.bodies)
    {
        const std::string bone = Text(body.GetComponent<Fenrir::RagdollBody>().bone);
        const bool inArm = bone == "upperarm_l" || bone == "lowerarm_l" || bone == "hand_l";
        INFO(bone);
        REQUIRE(body.GetComponent<Fenrir::RigidBody>().motionType ==
                (inArm ? Fenrir::MotionType::Dynamic : Fenrir::MotionType::Kinematic));
        REQUIRE(character.OverrideOf(body).has_value() == inArm);
        if (inArm)
        {
            const Fenrir::Joint* joint = JointTo(character.spawned, body);
            REQUIRE(joint != nullptr);
            const float gap = AnchorGap(*joint);
            INFO("its joint's anchors " << gap << " apart");
            REQUIRE(gap < 0.01f);
        }
        else
        {
            INFO("still on its bone");
            REQUIRE(
                Near(Fenrir::GetWorldTransform(body).pos, RagdollTest::OnBone(character.test.level, body).pos, 1e-4f));
        }
    }
    INFO("the arm was dragged along by the walking shoulder, a metre on");
    REQUIRE(Fenrir::GetWorldTransform(BodyOf(character.spawned, "hand_l")).pos.x > 0.7f);
    INFO("the root body follows, so the skeleton entity stays where it was put");
    REQUIRE(Fenrir::GetWorldTransform(character.skeleton).pos == place.pos);
    REQUIRE(Fenrir::GetWorldTransform(character.skeleton).rot == place.rot);
}

TEST_CASE("While simulating, the skeleton entity follows the root body horizontally and in yaw, keeping its height",
          "[ragdoll][simulate][root]")
{
    const Fenrir::Transform place(Vec3(2.0f, 0.25f, -1.0f), Fenrir::Math::AngleAxis(0.4f, Vec3(0.0f, 1.0f, 0.0f)),
                                  Vec3(1.0f));

    // The hips bone, by its body, measured against where the hips bone's base pose puts it under the entity.
    const auto check = [&place](Character& character, bool yaw) {
        const Entity& root = character.Ragdoll().rootBody;
        const Fenrir::Transform hips = RagdollTest::BoneFromBody(root);
        const Fenrir::Transform base = Decomposed(*Skeletons::GetBaseModel(
            character.test.level, character.skeleton, character.BoneOf(root), Fenrir::PoseKind::Simulation));
        const Fenrir::Transform entity = Fenrir::GetWorldTransform(character.skeleton);
        INFO("height kept, upright");
        REQUIRE(Near(entity.pos.y, place.pos.y, 1e-6f));
        REQUIRE(Upright(entity.rot));
        INFO("the hips' base pose under the entity lands on the hips bone horizontally");
        const Vec3 under = entity.pos + Fenrir::Math::Rotate(entity.rot, base.pos);
        REQUIRE(Near(under.x, hips.pos.x, 1e-3f));
        REQUIRE(Near(under.z, hips.pos.z, 1e-3f));
        if (yaw)
        {
            INFO("and, turning about the vertical, the entity faces where the hips' turn from their base pose faces");
            REQUIRE(Near(entity.rot, Heading(Fenrir::Math::Normalized(hips.rot * Fenrir::Math::Conjugate(base.rot))),
                         2e-3f));
        }
        return entity;
    };

    SECTION("spinning and drifting with no gravity: the entity turns and moves with the hips")
    {
        Character character(Weightless(), 0.0f, place);
        character.test.Run(2);
        character.Ragdoll().mode = RagdollMode::Simulating;
        // Every body turning about the vertical through the hips and drifting, as one.
        const Vec3 spin(0.0f, 2.0f, 0.0f);
        const Vec3 hips = Fenrir::GetWorldTransform(character.Ragdoll().rootBody).pos;
        for (const Entity& body : character.spawned.bodies)
        {
            Fenrir::RigidBody& rigid = body.GetComponent<Fenrir::RigidBody>();
            rigid.angularVelocity = spin;
            rigid.linearVelocity =
                Vec3(1.0f, 0.0f, 0.5f) + Fenrir::Math::Cross(spin, Fenrir::GetWorldTransform(body).pos - hips);
        }
        character.test.Run(30);

        const Fenrir::Transform entity = check(character, true);
        INFO("it has moved and turned");
        REQUIRE(Fenrir::Math::Magnitude(entity.pos - place.pos) > 0.2f);
        REQUIRE_FALSE(Near(entity.rot, place.rot, 1e-2f));
    }

    SECTION("falling to the floor at two and a half ticks a frame: the entity keeps its height under the hips")
    {
        Fenrir::PhysicsConfig config = RagdollTest::StepPosesOnly();
        config.interpolate = true;
        Character character(config, 0.0f, place);
        character.test.Run(2);
        character.Ragdoll().mode = RagdollMode::Simulating;
        character.Ragdoll().rootBody.GetComponent<Fenrir::RigidBody>().linearVelocity = Vec3(0.0f, 0.0f, 2.0f);
        character.test.Run(36, 2.5 * RagdollTest::Tick);
        check(character, false);
        INFO("the hips lie near the floor, far below where the entity's height would put them standing");
        REQUIRE(Fenrir::GetWorldTransform(character.Ragdoll().rootBody).pos.y < 0.6f);
    }

    SECTION("followRoot false: the entity stays where it was put")
    {
        Character character(RagdollTest::StepPosesOnly(), 0.0f, place);
        character.Ragdoll().followRoot = false;
        character.Ragdoll().mode = RagdollMode::Simulating;
        character.Ragdoll().rootBody.GetComponent<Fenrir::RigidBody>().linearVelocity = Vec3(0.0f, 0.0f, 2.0f);
        character.test.Run(60);
        REQUIRE(Fenrir::GetWorldTransform(character.skeleton).pos == place.pos);
        REQUIRE(Fenrir::GetWorldTransform(character.skeleton).rot == place.rot);
    }

    SECTION("following: the entity stays where it was put")
    {
        Character character(RagdollTest::StepPosesOnly(), 1.0f, place);
        character.test.Run(60);
        REQUIRE(Fenrir::GetWorldTransform(character.skeleton).pos == place.pos);
        REQUIRE(Fenrir::GetWorldTransform(character.skeleton).rot == place.rot);
    }
}

TEST_CASE("Following again snaps the bodies to the pose in one tick; the entries hold until every weight reads 0",
          "[ragdoll][simulate][getup]")
{
    Character character;
    const std::int32_t pelvis = character.Bone("pelvis");
    const std::int32_t hand = character.Bone("hand_l");
    character.Ragdoll().mode = RagdollMode::Simulating;
    character.test.Run(60);
    std::vector<Fenrir::Target> held;
    for (const Entity& body : character.spawned.bodies)
    {
        held.push_back(*character.OverrideOf(body));
    }

    character.Ragdoll().mode = RagdollMode::Following;
    character.test.Run(1);
    for (std::size_t i = 0; i < character.spawned.bodies.size(); ++i)
    {
        const Entity& body = character.spawned.bodies[i];
        INFO(Text(body.GetComponent<Fenrir::RagdollBody>().bone));
        INFO("kinematic, moved by the tick onto its bone");
        REQUIRE(body.GetComponent<Fenrir::RigidBody>().motionType == Fenrir::MotionType::Kinematic);
        const Fenrir::Transform expected = RagdollTest::OnBone(character.test.level, body);
        REQUIRE(Near(Fenrir::GetWorldTransform(body).pos, expected.pos, 1e-4f));
        REQUIRE(Near(Fenrir::GetWorldTransform(body).rot, expected.rot, 1e-4f));
        const std::optional<Fenrir::Math::AABB> bounds = Fenrir::PhysicsWorld::Of(character.test.level)->Bounds(body);
        REQUIRE(bounds.has_value());
        INFO("physics has it there too");
        REQUIRE(Near((bounds->min + bounds->max) * 0.5f, expected.pos, 1e-3f));
        INFO("its entry holds the last simulated pose");
        const std::optional<Fenrir::Target> entry = character.OverrideOf(body);
        REQUIRE(entry.has_value());
        REQUIRE(entry->trs.pos == held[i].trs.pos);
        REQUIRE(entry->trs.rot == held[i].trs.rot);
    }

    // Gameplay ramps the weights down; the entries hold while any reads more than 0.
    REQUIRE(Skeletons::SetOverrideWeight(character.test.level, character.skeleton, pelvis, 0.5f,
                                         Fenrir::OverrideScope::BoneSubtree));
    character.test.Run(5);
    REQUIRE(Skeletons::SetOverrideWeight(character.test.level, character.skeleton, pelvis, 0.0f,
                                         Fenrir::OverrideScope::BoneSubtree));
    REQUIRE(Skeletons::SetOverrideWeight(character.test.level, character.skeleton, hand, 0.1f));
    character.test.Run(5);
    for (const Entity& body : character.spawned.bodies)
    {
        INFO("held while the hand reads 0.1: " << Text(body.GetComponent<Fenrir::RagdollBody>().bone));
        REQUIRE(character.OverrideOf(body).has_value());
    }

    REQUIRE(Skeletons::SetOverrideWeight(character.test.level, character.skeleton, hand, 0.0f));
    character.test.Run(1);
    for (const Entity& body : character.spawned.bodies)
    {
        INFO("cleared once every weight reads 0: " << Text(body.GetComponent<Fenrir::RagdollBody>().bone));
        REQUIRE_FALSE(character.OverrideOf(body).has_value());
        INFO("and the weights are gameplay's still");
        REQUIRE(Skeletons::GetOverrideWeight(character.test.level, character.skeleton, character.BoneOf(body)) == 0.0f);
    }
    REQUIRE(character.test.warnings->empty());
}

TEST_CASE("Ragdolls::ClearOverrides clears the ragdoll's entries at once, leaving other bones' and the weights",
          "[ragdoll][simulate][getup]")
{
    Character character;
    const std::int32_t neck = character.Bone("neck_01");
    Fenrir::Target own;
    own.trs.rot = Fenrir::Math::AngleAxis(0.2f, Vec3(1.0f, 0.0f, 0.0f));
    REQUIRE(Skeletons::SetOverride(character.test.level, character.skeleton, neck, own));
    character.Ragdoll().mode = RagdollMode::Simulating;
    character.test.Run(10);
    character.Ragdoll().mode = RagdollMode::Following;
    character.test.Run(1);
    REQUIRE(character.OverrideOf(character.Ragdoll().rootBody).has_value());

    REQUIRE(Fenrir::Ragdolls::ClearOverrides(character.test.level, character.skeleton));
    for (const Entity& body : character.spawned.bodies)
    {
        INFO(Text(body.GetComponent<Fenrir::RagdollBody>().bone));
        REQUIRE_FALSE(character.OverrideOf(body).has_value());
        REQUIRE(Skeletons::GetOverrideWeight(character.test.level, character.skeleton, character.BoneOf(body)) == 1.0f);
    }
    INFO("game code's entry on a bone with no body stays");
    REQUIRE(Skeletons::GetOverride(character.test.level, character.skeleton, neck).has_value());
    character.test.Run(5);
    REQUIRE_FALSE(character.OverrideOf(character.Ragdoll().rootBody).has_value());

    INFO("an entity with no Ragdoll: nothing to clear");
    REQUIRE_FALSE(Fenrir::Ragdolls::ClearOverrides(character.test.level, character.spawned.parent));
}

TEST_CASE("A body destroyed while simulating leaves its bone at its last override", "[ragdoll][simulate][lifetime]")
{
    Character character;
    character.Ragdoll().mode = RagdollMode::Simulating;
    character.test.Run(20);
    const Entity hand = BodyOf(character.spawned, "hand_l");
    const std::int32_t handBone = character.BoneOf(hand);
    const Fenrir::Target last = *character.OverrideOf(hand);
    const Fenrir::Target lowerBefore = *character.OverrideOf(BodyOf(character.spawned, "lowerarm_l"));
    for (const Entity& joint : character.spawned.joints)
    {
        if (joint.GetComponent<Fenrir::Joint>().bodyB == hand)
        {
            character.test.Entities().DestroyEntity(joint);
        }
    }
    character.test.Entities().DestroyEntity(hand);
    character.test.Run(30);

    const std::optional<Fenrir::Target> entry =
        Skeletons::GetOverride(character.test.level, character.skeleton, handBone);
    REQUIRE(entry.has_value());
    REQUIRE(entry->trs.pos == last.trs.pos);
    REQUIRE(entry->trs.rot == last.trs.rot);
    INFO("the rest are still written");
    REQUIRE(character.OverrideOf(BodyOf(character.spawned, "lowerarm_l"))->trs.pos != lowerBefore.trs.pos);
    REQUIRE(character.Ragdoll().state.bodies == static_cast<std::int32_t>(character.spawned.bodies.size()) - 1);
}

TEST_CASE("Removing Ragdoll leaves the bodies as inactive ragdoll bodies, each warned about once",
          "[ragdoll][simulate][lifetime]")
{
    Character character;
    character.Ragdoll().mode = RagdollMode::Simulating;
    character.test.Run(10);
    const Fenrir::Target last = *character.OverrideOf(character.spawned.bodies.front());
    character.skeleton.RemoveComponent<Fenrir::Ragdoll>();
    character.test.Run(10);

    for (const Entity& body : character.spawned.bodies)
    {
        REQUIRE(body.GetComponent<Fenrir::RagdollBody>().state.bone == Fenrir::NoBone);
    }
    REQUIRE(character.test.Warnings("is skipped: its ragdoll is not a live entity") == character.spawned.bodies.size());
    INFO("nothing writes the entries any more");
    REQUIRE(Skeletons::GetOverride(character.test.level, character.skeleton, character.Bone("pelvis"))->trs.pos ==
            last.trs.pos);
}

TEST_CASE("A sleeping ragdoll reads anyAwake false and is still written each frame", "[ragdoll][simulate][sleep]")
{
    Character character;
    character.Ragdoll().mode = RagdollMode::Simulating;
    REQUIRE(character.RunUntilAsleep(1200));
    REQUIRE(Fenrir::PhysicsWorld::Of(character.test.level)->IsSleeping(character.Ragdoll().rootBody));

    REQUIRE(Skeletons::ClearOverrides(character.test.level, character.skeleton));
    character.test.Run(1);
    REQUIRE_FALSE(character.Ragdoll().state.anyAwake);
    for (const Entity& body : character.spawned.bodies)
    {
        INFO(Text(body.GetComponent<Fenrir::RagdollBody>().bone));
        const std::optional<Fenrir::Target> entry = character.OverrideOf(body);
        REQUIRE(entry.has_value());
        REQUIRE(Near(entry->trs.pos, RagdollTest::BoneFromBody(body).pos, 1e-5f));
    }
}

TEST_CASE("An inactive scene's ragdoll keeps its state", "[ragdoll][simulate][scenes]")
{
    Character character;
    character.Ragdoll().mode = RagdollMode::Simulating;
    character.test.Run(10);
    const Fenrir::RagdollState state = character.Ragdoll().state;
    const Fenrir::Target entry = *character.OverrideOf(character.Ragdoll().rootBody);
    const Fenrir::Transform entity = Fenrir::GetWorldTransform(character.skeleton);

    character.test.app.CreateScene("Other");
    character.test.app.ChangeActiveScene("Other");
    character.test.Run(30);
    character.test.app.ChangeActiveScene("Level");

    REQUIRE(character.Ragdoll().state.anyAwake == state.anyAwake);
    REQUIRE(character.Ragdoll().state.bodies == state.bodies);
    REQUIRE(character.Ragdoll().state.inactive == state.inactive);
    REQUIRE(character.OverrideOf(character.Ragdoll().rootBody)->trs.pos == entry.trs.pos);
    REQUIRE(Fenrir::GetWorldTransform(character.skeleton).pos == entity.pos);
}
