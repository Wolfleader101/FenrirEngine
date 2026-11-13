// Simulating (ADR-0038, ADR-0054): a ragdoll going limp without moving, keeping its following velocity, its bodies
// falling and settling; each simulating body's bone written as a world pose override through offset⁻¹, at gameplay's
// weights, never the glue's; a partial ragdoll's arm hanging from its following shoulder; the skeleton entity following
// the root body horizontally and in yaw; getting up, the entries held until every weight reads 0 and cleared at once
// by Ragdolls::ClearOverrides; and the lifetime, sleep and scene rules.

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
using RagdollTest::Character;
using RagdollTest::Decomposed;
using RagdollTest::JointTo;
using RagdollTest::Near;
using RagdollTest::Text;
using RagdollTest::Weightless;

namespace
{
    // Where the render pose has `bone` now, in world space.
    Fenrir::Transform RenderBone(Character& character, std::int32_t bone)
    {
        return Decomposed(*Skeletons::GetBoneWorldMatrix(character.test.level, character.skeleton, bone));
    }

    // The rotation `rotation` turns +Z to, flattened onto the ground, as a turn about +Y: for a rotation that turns
    // about the vertical only.
    Quat Heading(const Quat& rotation)
    {
        const Vec3 forward = Fenrir::Math::Rotate(rotation, Vec3(0.0f, 0.0f, 1.0f));
        return Fenrir::Math::AngleAxis(std::atan2(forward.x, forward.z), Vec3(0.0f, 1.0f, 0.0f));
    }

    bool Upright(const Quat& rotation)
    {
        return Near(Fenrir::Math::Rotate(rotation, Vec3(0.0f, 1.0f, 0.0f)), Vec3(0.0f, 1.0f, 0.0f), 1e-4f);
    }

    // How far apart a joint's two anchors are: where each body puts it.
    float AnchorGap(const Fenrir::Joint& joint)
    {
        const Vec3 a = Vec3(Fenrir::GetWorldMatrix(joint.bodyA) * Fenrir::Math::Vec4(joint.frameA.position, 1.0f));
        const Vec3 b = Vec3(Fenrir::GetWorldMatrix(joint.bodyB) * Fenrir::Math::Vec4(joint.frameB.position, 1.0f));
        return Fenrir::Math::Magnitude(a - b);
    }

    // Checks the skeleton entity against the root body: at `height`, upright, the hips bone's base pose under it on
    // the root body's bone horizontally. Returns the entity's world transform.
    Fenrir::Transform CheckUnderRoot(Character& character, float height)
    {
        const Entity& root = character.Ragdoll().rootBody;
        const Fenrir::Transform hips = RagdollTest::BoneFromBody(root);
        const Fenrir::Transform base = Decomposed(*Skeletons::GetBaseModel(
            character.test.level, character.skeleton, character.BoneOf(root), Fenrir::PoseKind::Simulation));
        const Fenrir::Transform entity = Fenrir::GetWorldTransform(character.skeleton);
        INFO("the entity kept its height: " << entity.pos.y);
        REQUIRE(Near(entity.pos.y, height, 1e-6f));
        INFO("the entity stands upright");
        REQUIRE(Upright(entity.rot));
        const Vec3 under = entity.pos + Fenrir::Math::Rotate(entity.rot, base.pos);
        INFO("the hips' base pose under the entity, (" << under.x << ", " << under.z << "), lands on the hips bone, ("
                                                       << hips.pos.x << ", " << hips.pos.z << "), horizontally");
        REQUIRE(Near(under.x, hips.pos.x, 1e-3f));
        REQUIRE(Near(under.z, hips.pos.z, 1e-3f));
        return entity;
    }
} // namespace

TEST_CASE("Simulating makes every body dynamic where it is, keeping its following velocity", "[ragdoll][simulate]")
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
        {
            INFO("the body is dynamic");
            REQUIRE(body.GetComponent<Fenrir::RigidBody>().motionType == Fenrir::MotionType::Dynamic);
        }
        {
            INFO("its effective mode reads simulating");
            REQUIRE(body.GetComponent<Fenrir::RagdollBody>().state.mode == RagdollMode::Simulating);
        }
        const Vec3 moved = Fenrir::GetWorldTransform(body).pos - before[i];
        INFO("it moved (" << moved.x << ", " << moved.y << ", " << moved.z << ") over the step");
        {
            INFO("one step on at the walk's speed, not put anywhere by the switch");
            REQUIRE(Near(moved.x, Walk * static_cast<float>(RagdollTest::Tick), 2e-3f));
            REQUIRE(Near(moved.z, 0.0f, 2e-3f));
        }
        {
            INFO("and gravity's one step down, g dt²");
            REQUIRE(Near(moved.y, -9.81f * static_cast<float>(RagdollTest::Tick * RagdollTest::Tick), 1e-3f));
        }
        {
            INFO("its velocity still the walk's: " << body.GetComponent<Fenrir::RigidBody>().linearVelocity.x);
            REQUIRE(Near(body.GetComponent<Fenrir::RigidBody>().linearVelocity.x, Walk, 0.1f));
        }
    }
    INFO("nothing was warned about");
    REQUIRE(character.test.warnings->empty());
}

TEST_CASE("Simulating bodies tumble forward, fall onto the floor and settle", "[ragdoll][simulate]")
{
    Character character(RagdollTest::StepPosesOnly(), 1.0f);
    character.test.Run(30);
    const float startX = Fenrir::GetWorldTransform(character.Ragdoll().rootBody).pos.x;
    character.Ragdoll().mode = RagdollMode::Simulating;

    {
        INFO("every body fell asleep within twenty seconds");
        REQUIRE(character.RunUntilAsleep(1200));
    }
    for (const Entity& body : character.spawned.bodies)
    {
        const Vec3 at = Fenrir::GetWorldTransform(body).pos;
        INFO(Text(body.GetComponent<Fenrir::RagdollBody>().bone)
             << " rests at (" << at.x << ", " << at.y << ", " << at.z << "): on the floor, lying down");
        REQUIRE(at.y > 0.0f);
        REQUIRE(at.y < 0.6f);
    }
    const float endX = Fenrir::GetWorldTransform(character.Ragdoll().rootBody).pos.x;
    INFO("the hips carried on forward, from " << startX << " to " << endX);
    REQUIRE(endX > startX + 0.1f);
}

TEST_CASE("Each simulating body puts its bone, in the render pose, where the body is through offset⁻¹, at gameplay's "
          "weights",
          "[ragdoll][simulate][override]")
{
    // Interpolation on, frames of a tick and a half and of under half a tick (no tick at all, some frames): the bones
    // follow the bodies as they are drawn.
    Fenrir::PhysicsConfig config = RagdollTest::StepPosesOnly();
    config.interpolate = true;
    Character character(config);
    const std::int32_t head = character.Bone("Head");
    REQUIRE(Skeletons::SetOverrideWeight(character.test.level, character.skeleton, head, 0.3f));
    character.test.Run(5);
    character.Ragdoll().mode = RagdollMode::Simulating;

    for (int frame = 0; frame < 20; ++frame)
    {
        character.test.Run(1, (frame % 2 == 0 ? 1.5 : 0.4) * RagdollTest::Tick);
        for (const Entity& body : character.spawned.bodies)
        {
            const std::int32_t bone = character.BoneOf(body);
            if (bone == head)
            {
                continue; // at 0.3: not on its body
            }
            const Fenrir::Transform expected = RagdollTest::BoneFromBody(body);
            const Fenrir::Transform actual = RenderBone(character, bone);
            INFO("frame " << frame << ", " << Text(body.GetComponent<Fenrir::RagdollBody>().bone));
            {
                INFO("the bone is drawn where its body puts it");
                REQUIRE(Near(actual.pos, expected.pos, 1e-4f));
                REQUIRE(Near(actual.rot, expected.rot, 1e-4f));
            }
            const std::optional<Fenrir::Target> entry = character.OverrideOf(body);
            {
                INFO("the glue set the bone's entry");
                REQUIRE(entry.has_value());
            }
            {
                INFO("as a world target of position and rotation at weight 1");
                REQUIRE(entry->space == Fenrir::PoseSpace::World);
                REQUIRE(entry->channels == (Fenrir::Channels::Translation | Fenrir::Channels::Rotation));
                REQUIRE(entry->weight == 1.0f);
            }
        }
    }

    // A subtree at 0.5: the lower arm halfway, in its parent's space, from its animated local to the body's.
    const std::int32_t lowerArm = character.Bone("lowerarm_l");
    REQUIRE(Skeletons::SetOverrideWeight(character.test.level, character.skeleton, lowerArm, 0.5f,
                                         Fenrir::OverrideScope::BoneSubtree));
    character.test.Run(1, 1.5 * RagdollTest::Tick);
    const Fenrir::Transform target = RagdollTest::BoneFromBody(BodyOf(character.spawned, "lowerarm_l"));
    const Fenrir::Math::Mat4 parentWorld =
        *Skeletons::GetBoneWorldMatrix(character.test.level, character.skeleton, character.Bone("upperarm_l"));
    const Fenrir::Transform wanted = Decomposed(Fenrir::Math::Inverse(parentWorld) * Fenrir::TransformToMat4(target));
    const Fenrir::Transform animated = *Skeletons::GetBaseLocal(character.test.level, character.skeleton, lowerArm);
    const Fenrir::Transform blended = *Skeletons::GetLocal(character.test.level, character.skeleton, lowerArm);
    {
        INFO("at weight 0.5 the lower arm's local is halfway between its animated pose and its body's");
        REQUIRE(Near(blended.pos, (animated.pos + wanted.pos) * 0.5f, 1e-4f));
        REQUIRE(Near(blended.rot, Fenrir::Math::Slerp(animated.rot, wanted.rot, 0.5f), 1e-4f));
    }

    for (const Entity& body : character.spawned.bodies)
    {
        const std::int32_t bone = character.BoneOf(body);
        const bool inArm = bone == lowerArm || bone == character.Bone("hand_l");
        const float expected = bone == head ? 0.3f : (inArm ? 0.5f : 1.0f);
        INFO("the glue changed no weight: " << Text(body.GetComponent<Fenrir::RagdollBody>().bone) << " reads "
                                            << character.WeightOf(body) << ", gameplay set " << expected);
        REQUIRE(character.WeightOf(body) == expected);
    }
    INFO("nothing was warned about");
    REQUIRE(character.test.warnings->empty());
}

TEST_CASE("A simulating body under a scaled skeleton entity puts its bone where the body is", "[ragdoll][simulate]")
{
    const Fenrir::Transform place(Vec3(0.0f), Quat(1.0f, 0.0f, 0.0f, 0.0f), Vec3(1.5f));
    Character character(RagdollTest::StepPosesOnly(), 0.0f, place);
    character.Ragdoll().mode = RagdollMode::Simulating;
    character.test.Run(20);

    for (const Entity& body : character.spawned.bodies)
    {
        const Fenrir::Transform expected = RagdollTest::BoneFromBody(body, Vec3(1.5f));
        const Fenrir::Transform actual = RenderBone(character, character.BoneOf(body));
        INFO(Text(body.GetComponent<Fenrir::RagdollBody>().bone)
             << ": the bone is drawn where its body puts it, through the offset at the skeleton's scale");
        REQUIRE(Near(actual.pos, expected.pos, 1e-4f));
        REQUIRE(Near(actual.rot, expected.rot, 1e-4f));
    }
    INFO("following the root kept the entity's scale");
    REQUIRE(Near(Fenrir::GetWorldTransform(character.skeleton).scale, Vec3(1.5f), 1e-6f));
}

TEST_CASE("A simulating arm under a following shoulder hangs from its joint without stretching, and only its bones get "
          "overrides",
          "[ragdoll][simulate][partial]")
{
    Character character(RagdollTest::StepPosesOnly(), 1.0f);
    for (const std::string_view bone : {"upperarm_l", "lowerarm_l", "hand_l"})
    {
        BodyOf(character.spawned, bone).GetComponent<Fenrir::RagdollBody>().mode = Fenrir::RagdollBodyMode::Simulating;
    }
    const Fenrir::Transform place = Fenrir::GetWorldTransform(character.skeleton);
    character.test.Run(60);

    for (const Entity& body : character.spawned.bodies)
    {
        const std::string bone = Text(body.GetComponent<Fenrir::RagdollBody>().bone);
        const bool inArm = bone == "upperarm_l" || bone == "lowerarm_l" || bone == "hand_l";
        INFO(bone << (inArm ? ", in the arm" : ", not in the arm"));
        {
            INFO("dynamic in the arm, kinematic elsewhere");
            REQUIRE(body.GetComponent<Fenrir::RigidBody>().motionType ==
                    (inArm ? Fenrir::MotionType::Dynamic : Fenrir::MotionType::Kinematic));
        }
        {
            INFO("an override entry on the arm's bones only");
            REQUIRE(character.OverrideOf(body).has_value() == inArm);
        }
        if (inArm)
        {
            const Fenrir::Joint* joint = JointTo(character.spawned, body);
            {
                INFO("the body has a joint to its parent");
                REQUIRE(joint != nullptr);
            }
            const float gap = AnchorGap(*joint);
            INFO("the joint's anchors are " << gap << " apart: the arm hangs from it without stretching");
            REQUIRE(gap < 0.01f);
        }
        else
        {
            INFO("the body is still on its bone");
            REQUIRE(
                Near(Fenrir::GetWorldTransform(body).pos, RagdollTest::OnBone(character.test.level, body).pos, 1e-4f));
        }
    }
    {
        INFO("the arm was dragged along by the walking shoulder, a metre on");
        REQUIRE(Fenrir::GetWorldTransform(BodyOf(character.spawned, "hand_l")).pos.x > 0.7f);
    }
    INFO("the root body follows, so the skeleton entity stays where it was put");
    REQUIRE(Fenrir::GetWorldTransform(character.skeleton).pos == place.pos);
    REQUIRE(Fenrir::GetWorldTransform(character.skeleton).rot == place.rot);
}

TEST_CASE("While simulating, the skeleton entity follows the root body horizontally and in yaw, keeping its height",
          "[ragdoll][simulate][root]")
{
    const Vec3 up(0.0f, 1.0f, 0.0f);
    const Fenrir::Transform place(Vec3(2.0f, 0.25f, -1.0f), Fenrir::Math::AngleAxis(0.4f, up), Vec3(1.0f));

    SECTION("spinning and drifting with no gravity: the entity turns and moves with the hips")
    {
        Character character(Weightless(), 0.0f, place);
        character.test.Run(2);
        character.Ragdoll().mode = RagdollMode::Simulating;
        character.Throw(Vec3(0.0f, 2.0f, 0.0f), Vec3(1.0f, 0.0f, 0.5f));
        character.test.Run(30);

        const Fenrir::Transform entity = CheckUnderRoot(character, place.pos.y);
        const Entity& root = character.Ragdoll().rootBody;
        const Quat base = Decomposed(*Skeletons::GetBaseModel(character.test.level, character.skeleton,
                                                              character.BoneOf(root), Fenrir::PoseKind::Simulation))
                              .rot;
        const Quat turn =
            Heading(Fenrir::Math::Normalized(RagdollTest::BoneFromBody(root).rot * Fenrir::Math::Conjugate(base)));
        {
            INFO("the entity faces where the hips' turn from their base pose faces");
            REQUIRE(Near(entity.rot, turn, 2e-3f));
        }
        INFO("it has moved and turned");
        REQUIRE(Fenrir::Math::Magnitude(entity.pos - place.pos) > 0.2f);
        REQUIRE_FALSE(Near(entity.rot, place.rot, 1e-2f));
    }

    SECTION("tipping onto its front or back with no gravity: the entity keeps facing the way the body faced")
    {
        // 150° about the character's own right, forwards (onto its front and past) or backwards (onto its back and
        // past): beyond a quarter turn, where the flattened forward points the other way.
        const Vec3 right = Fenrir::Math::Rotate(place.rot, Vec3(1.0f, 0.0f, 0.0f));
        for (const float direction : {1.0f, -1.0f})
        {
            Character character(Weightless(), 0.0f, place);
            character.test.Run(2);
            character.Ragdoll().mode = RagdollMode::Simulating;
            character.Throw(right * (direction * 5.23598776f), Vec3(0.0f)); // 150° over the half second
            character.test.Run(30);

            const Fenrir::Transform entity = CheckUnderRoot(character, place.pos.y);
            const Vec3 head = Fenrir::Math::Rotate(Fenrir::GetWorldTransform(character.Ragdoll().rootBody).rot, up);
            INFO((direction > 0.0f ? "on its front" : "on its back")
                 << ", its spine pointing (" << head.x << ", " << head.y << ", " << head.z << ")");
            {
                INFO("the body is past lying flat, head lower than hips");
                REQUIRE(head.y < -0.5f);
            }
            INFO("and the entity still faces the way it did before the body tipped over");
            REQUIRE(Near(entity.rot, place.rot, 2e-3f));
        }
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
        CheckUnderRoot(character, place.pos.y);
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
        INFO("the entity was not moved");
        REQUIRE(Fenrir::GetWorldTransform(character.skeleton).pos == place.pos);
        REQUIRE(Fenrir::GetWorldTransform(character.skeleton).rot == place.rot);
    }

    SECTION("following: the entity stays where it was put")
    {
        Character character(RagdollTest::StepPosesOnly(), 1.0f, place);
        character.test.Run(60);
        INFO("the entity was not moved");
        REQUIRE(Fenrir::GetWorldTransform(character.skeleton).pos == place.pos);
        REQUIRE(Fenrir::GetWorldTransform(character.skeleton).rot == place.rot);
    }
}

TEST_CASE("Following again snaps the bodies to the pose over one tick, their entries holding the last simulated pose",
          "[ragdoll][simulate][getup]")
{
    Character character;
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
        {
            INFO("the body is kinematic");
            REQUIRE(body.GetComponent<Fenrir::RigidBody>().motionType == Fenrir::MotionType::Kinematic);
        }
        const Fenrir::Transform expected = RagdollTest::OnBone(character.test.level, body);
        {
            INFO("it was put on its bone");
            REQUIRE(Near(Fenrir::GetWorldTransform(body).pos, expected.pos, 1e-4f));
            REQUIRE(Near(Fenrir::GetWorldTransform(body).rot, expected.rot, 1e-4f));
        }
        const std::optional<Fenrir::Math::AABB> bounds = Fenrir::PhysicsWorld::Of(character.test.level)->Bounds(body);
        {
            INFO("physics has a body for it");
            REQUIRE(bounds.has_value());
        }
        {
            INFO("and the step moved that body there");
            REQUIRE(Near((bounds->min + bounds->max) * 0.5f, expected.pos, 1e-3f));
        }
        const std::optional<Fenrir::Target> entry = character.OverrideOf(body);
        INFO("its entry still holds the last simulated pose, for the blend back");
        REQUIRE(entry.has_value());
        REQUIRE(entry->trs.pos == held[i].trs.pos);
        REQUIRE(entry->trs.rot == held[i].trs.rot);
    }
}

TEST_CASE("Once every body follows, the entries hold while any weight is above 0 and are cleared when all read 0",
          "[ragdoll][simulate][getup]")
{
    Character character;
    const std::int32_t pelvis = character.Bone("pelvis");
    const std::int32_t hand = character.Bone("hand_l");
    character.Ragdoll().mode = RagdollMode::Simulating;
    character.test.Run(30);
    character.Ragdoll().mode = RagdollMode::Following;

    // Gameplay ramps the weights down.
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
        INFO(Text(body.GetComponent<Fenrir::RagdollBody>().bone));
        {
            INFO("its entry was cleared once every weight read 0");
            REQUIRE_FALSE(character.OverrideOf(body).has_value());
        }
        INFO("and its weight is gameplay's 0 still");
        REQUIRE(character.WeightOf(body) == 0.0f);
    }
    INFO("nothing was warned about");
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
    {
        INFO("the entries hold the last simulated pose, at weight 1");
        REQUIRE(character.OverrideOf(character.Ragdoll().rootBody).has_value());
    }

    {
        INFO("the skeleton has a Ragdoll to clear");
        REQUIRE(Fenrir::Ragdolls::ClearOverrides(character.test.level, character.skeleton));
    }
    for (const Entity& body : character.spawned.bodies)
    {
        INFO(Text(body.GetComponent<Fenrir::RagdollBody>().bone));
        {
            INFO("its entry went at once");
            REQUIRE_FALSE(character.OverrideOf(body).has_value());
        }
        INFO("and its weight stayed 1");
        REQUIRE(character.WeightOf(body) == 1.0f);
    }
    {
        INFO("game code's entry on a bone with no body stays");
        REQUIRE(Skeletons::GetOverride(character.test.level, character.skeleton, neck).has_value());
    }
    character.test.Run(5);
    {
        INFO("nothing writes the entries again while every body follows");
        REQUIRE_FALSE(character.OverrideOf(character.Ragdoll().rootBody).has_value());
    }
    INFO("an entity with no Ragdoll has nothing to clear");
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
    {
        INFO("the hand bone keeps its last entry, unchanged");
        REQUIRE(entry.has_value());
        REQUIRE(entry->trs.pos == last.trs.pos);
        REQUIRE(entry->trs.rot == last.trs.rot);
    }
    {
        INFO("the other bodies' bones are still written");
        REQUIRE(character.OverrideOf(BodyOf(character.spawned, "lowerarm_l"))->trs.pos != lowerBefore.trs.pos);
    }
    INFO("the ragdoll drives one body fewer: " << character.Ragdoll().state.bodies);
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
        INFO(Text(body.GetComponent<Fenrir::RagdollBody>().bone) << " is skipped, its bone unresolved");
        REQUIRE(body.GetComponent<Fenrir::RagdollBody>().state.bone == Fenrir::NoBone);
    }
    {
        INFO("one warning per body over ten frames");
        REQUIRE(character.test.Warnings("is skipped: its ragdoll is not a live entity") ==
                character.spawned.bodies.size());
    }
    INFO("nothing writes the entries any more");
    REQUIRE(Skeletons::GetOverride(character.test.level, character.skeleton, character.Bone("pelvis"))->trs.pos ==
            last.trs.pos);
}

TEST_CASE("A sleeping ragdoll reads anyAwake false and is still written each frame", "[ragdoll][simulate][sleep]")
{
    Character character;
    character.Ragdoll().mode = RagdollMode::Simulating;
    {
        INFO("the ragdoll fell asleep within twenty seconds");
        REQUIRE(character.RunUntilAsleep(1200));
    }
    {
        INFO("its root body sleeps");
        REQUIRE(Fenrir::PhysicsWorld::Of(character.test.level)->IsSleeping(character.Ragdoll().rootBody));
    }

    REQUIRE(Skeletons::ClearOverrides(character.test.level, character.skeleton));
    character.test.Run(1);
    {
        INFO("anyAwake reads false");
        REQUIRE_FALSE(character.Ragdoll().state.anyAwake);
    }
    for (const Entity& body : character.spawned.bodies)
    {
        INFO(Text(body.GetComponent<Fenrir::RagdollBody>().bone) << ": written again, asleep, where its body is");
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

    {
        INFO("its state is as it was");
        REQUIRE(character.Ragdoll().state.anyAwake == state.anyAwake);
        REQUIRE(character.Ragdoll().state.bodies == state.bodies);
        REQUIRE(character.Ragdoll().state.inactive == state.inactive);
    }
    {
        INFO("its entries are as they were");
        REQUIRE(character.OverrideOf(character.Ragdoll().rootBody)->trs.pos == entry.trs.pos);
    }
    INFO("its skeleton entity was not moved");
    REQUIRE(Fenrir::GetWorldTransform(character.skeleton).pos == entity.pos);
}
