// DescribeHumanoid on the code-built humanoid: the parts, shapes, masses, joints and offsets the spec gives, partial
// maps, and the maps it refuses.

#include <cmath>
#include <cstddef>
#include <numbers>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "FenrirRagdoll/Description.hpp"
#include "RagdollTest.hpp"

using Fenrir::CapsuleShape;
using Fenrir::HingeJoint;
using Fenrir::HumanoidBones;
using Fenrir::RagdollDescription;
using Fenrir::RagdollPart;
using Fenrir::SwingTwistJoint;
using Fenrir::Math::Vec3;
using RagdollTest::Near;
using RagdollTest::Text;

namespace
{
    constexpr float Deg = std::numbers::pi_v<float> / 180.0f;
    // The rig's stature: Head (0, 1.7, 0) to foot_l (0.1, 0.1, 0).
    const float Stature = std::sqrt(0.1f * 0.1f + 1.6f * 1.6f);

    const RagdollPart* PartOf(const RagdollDescription& description, std::string_view bone)
    {
        for (const RagdollPart& part : description.parts)
        {
            if (Text(part.bone) == bone)
            {
                return &part;
            }
        }
        return nullptr;
    }

    struct Expected
    {
        const char* bone;
        const char* parent;
        float radius; // × stature
        float mass;   // × 70 kg
        int kind;     // 0 none (root), 1 hinge, 2 swing-twist
        float cone;   // degrees
        float twist;  // degrees, either way
    };

    // The spec's numbers, slot by slot.
    constexpr Expected Table[] = {
        {"pelvis", "", 0.065f, 0.14f, 0, 0.0f, 0.0f},
        {"spine_01", "pelvis", 0.055f, 0.12f, 2, 20.0f, 20.0f},
        {"spine_03", "spine_01", 0.06f, 0.22f, 2, 20.0f, 20.0f},
        {"Head", "spine_03", 0.055f, 0.08f, 2, 30.0f, 45.0f},
        {"upperarm_l", "spine_03", 0.025f, 0.03f, 2, 80.0f, 45.0f},
        {"lowerarm_l", "upperarm_l", 0.022f, 0.02f, 1, 0.0f, 0.0f},
        {"hand_l", "lowerarm_l", 0.02f, 0.01f, 2, 35.0f, 20.0f},
        {"upperarm_r", "spine_03", 0.025f, 0.03f, 2, 80.0f, 45.0f},
        {"lowerarm_r", "upperarm_r", 0.022f, 0.02f, 1, 0.0f, 0.0f},
        {"hand_r", "lowerarm_r", 0.02f, 0.01f, 2, 35.0f, 20.0f},
        {"thigh_l", "pelvis", 0.038f, 0.10f, 2, 45.0f, 25.0f},
        {"calf_l", "thigh_l", 0.03f, 0.045f, 1, 0.0f, 0.0f},
        {"foot_l", "calf_l", 0.025f, 0.015f, 2, 30.0f, 10.0f},
        {"thigh_r", "pelvis", 0.038f, 0.10f, 2, 45.0f, 25.0f},
        {"calf_r", "thigh_r", 0.03f, 0.045f, 1, 0.0f, 0.0f},
        {"foot_r", "calf_r", 0.025f, 0.015f, 2, 30.0f, 10.0f},
    };

    RagdollDescription Describe(const HumanoidBones& bones, float mass = 70.0f)
    {
        RagdollTest::RagdollApp test;
        const Fenrir::Entity skeleton = test.AddSkeleton(RagdollTest::Humanoid());
        return Fenrir::DescribeHumanoid(test.DataOf(skeleton), bones, mass);
    }

    // Where a part's body is in model space at rest: bone × offset, from the rig's rest positions.
    Vec3 BodyCentre(const RagdollPart& part, const Vec3& bonePosition)
    {
        // The rig's bones are all unrotated at rest, so the bone's model rotation is the identity.
        return bonePosition + part.offset.pos;
    }
} // namespace

TEST_CASE("A full map gives sixteen parts with the spec's radii, masses, joints and limits", "[ragdoll][describe]")
{
    const RagdollDescription description = Describe(RagdollTest::HumanoidMap());

    INFO("the map is accepted: " << description.error);
    REQUIRE(description.error.empty());
    INFO("sixteen parts, in the slots' order, root first");
    REQUIRE(description.parts.size() == 16);
    for (std::size_t i = 0; i < 16; ++i)
    {
        const Expected& expected = Table[i];
        const RagdollPart& part = description.parts[i];
        INFO("part " << i << " is " << expected.bone << " under '" << expected.parent << "'");
        REQUIRE(Text(part.bone) == expected.bone);
        REQUIRE(Text(part.parent) == expected.parent);
        INFO(expected.bone << " is a capsule of radius " << expected.radius << " × the stature");
        REQUIRE(part.shape.Is<CapsuleShape>());
        REQUIRE(Near(part.shape.As<CapsuleShape>().radius, expected.radius * Stature, 1e-5f));
        INFO(expected.bone << " weighs " << expected.mass << " × 70 kg");
        REQUIRE(Near(part.mass, expected.mass * 70.0f, 1e-4f));
        if (expected.kind == 1)
        {
            INFO(expected.bone << " is a hinge bending 140 degrees one way from the rest pose");
            REQUIRE(part.joint.Is<HingeJoint>());
            const HingeJoint& hinge = part.joint.As<HingeJoint>();
            REQUIRE(hinge.limits.enabled);
            REQUIRE(Near(hinge.limits.max - hinge.limits.min, 140.0f * Deg, 1e-5f));
            REQUIRE((Near(hinge.limits.min, 0.0f) || Near(hinge.limits.max, 0.0f)));
            REQUIRE(Near(hinge.friction, 0.5f));
            REQUIRE(hinge.motor.mode == Fenrir::JointMotorMode::Off);
        }
        else if (expected.kind == 2)
        {
            INFO(expected.bone << " is a swing-twist joint with a " << expected.cone << " degree cone and +-"
                               << expected.twist << " degrees of twist");
            REQUIRE(part.joint.Is<SwingTwistJoint>());
            const SwingTwistJoint& joint = part.joint.As<SwingTwistJoint>();
            REQUIRE(Near(joint.swingYLimit, expected.cone * Deg, 1e-5f));
            REQUIRE(Near(joint.swingZLimit, expected.cone * Deg, 1e-5f));
            REQUIRE(Near(joint.twistMin, -expected.twist * Deg, 1e-5f));
            REQUIRE(Near(joint.twistMax, expected.twist * Deg, 1e-5f));
            REQUIRE(Near(joint.friction, 0.5f));
            REQUIRE(joint.motor.mode == Fenrir::JointMotorMode::Off);
        }
    }

    float total = 0.0f;
    for (const RagdollPart& part : description.parts)
    {
        total += part.mass;
    }
    INFO("the masses sum to the 70 kg asked for: " << total);
    REQUIRE(Near(total, 70.0f, 1e-3f));
    INFO("the description's defaults are the spec's");
    REQUIRE(description.group == 0);
    REQUIRE(Near(description.jointFriction, 0.5f));
    REQUIRE(Near(description.strength, 1.0f));
}

TEST_CASE("Capsules run from bone to child, leaf parts to their first child or along their parent",
          "[ragdoll][describe]")
{
    const RagdollDescription description = Describe(RagdollTest::HumanoidMap());
    REQUIRE(description.error.empty());

    // thigh_l (0.1, 0.95, 0) to calf_l (0.1, 0.5, 0): 0.45 long, centred between them.
    const RagdollPart& thigh = *PartOf(description, "thigh_l");
    const float thighRadius = 0.038f * Stature;
    INFO("the thigh's capsule spans thigh to calf: half-height 0.225 less its radius, centred at (0.1, 0.725, 0)");
    REQUIRE(Near(thigh.shape.As<CapsuleShape>().halfHeight, 0.225f - thighRadius, 1e-5f));
    REQUIRE(Near(BodyCentre(thigh, Vec3(0.1f, 0.95f, 0.0f)), Vec3(0.1f, 0.725f, 0.0f), 1e-5f));
    INFO("its capsule's axis (the body's Y) points down the leg");
    REQUIRE(Near(Fenrir::Math::Rotate(thigh.offset.rot, Vec3(0.0f, 1.0f, 0.0f)), Vec3(0.0f, -1.0f, 0.0f), 1e-5f));

    // The chest runs to the head slot's bone, over the unmapped neck: spine_03 (0, 1.4, 0) to Head (0, 1.7, 0).
    const RagdollPart& chest = *PartOf(description, "spine_03");
    INFO("the chest runs to the head's bone, past the unmapped neck: centred at (0, 1.55, 0)");
    REQUIRE(Near(BodyCentre(chest, Vec3(0.0f, 1.4f, 0.0f)), Vec3(0.0f, 1.55f, 0.0f), 1e-5f));

    // The hand is a leaf with a child bone: hand_l (0.2, 1, 0) to index_01_l (0.2, 0.9, 0).
    const RagdollPart& hand = *PartOf(description, "hand_l");
    INFO("the hand runs to its finger: 0.1 long, centred at (0.2, 0.95, 0)");
    REQUIRE(Near(BodyCentre(hand, Vec3(0.2f, 1.0f, 0.0f)), Vec3(0.2f, 0.95f, 0.0f), 1e-5f));
    REQUIRE(Near(hand.shape.As<CapsuleShape>().halfHeight, std::max(0.05f - 0.02f * Stature, 0.005f), 1e-5f));

    // The head is a leaf with no child: it runs on along neck_01 → Head (up) for 0.12 × the stature.
    const RagdollPart& head = *PartOf(description, "Head");
    const float headLength = 0.12f * Stature;
    INFO("the head runs up along its parent bone for 0.12 × the stature");
    REQUIRE(Near(BodyCentre(head, Vec3(0.0f, 1.7f, 0.0f)), Vec3(0.0f, 1.7f + 0.5f * headLength, 0.0f), 1e-5f));
    REQUIRE(
        Near(head.shape.As<CapsuleShape>().halfHeight, std::max(0.5f * headLength - 0.055f * Stature, 0.005f), 1e-5f));

    // The foot runs to its ball: foot_l (0.1, 0.1, 0) to ball_l (0.1, 0.05, 0.15).
    const RagdollPart& foot = *PartOf(description, "foot_l");
    INFO("the foot runs forward to its ball");
    REQUIRE(Near(BodyCentre(foot, Vec3(0.1f, 0.1f, 0.0f)), Vec3(0.1f, 0.075f, 0.075f), 1e-5f));
}

TEST_CASE("Joints sit at each bone's origin, twisting along the part, the elbows bending forward and the knees back",
          "[ragdoll][describe]")
{
    const RagdollDescription description = Describe(RagdollTest::HumanoidMap());
    REQUIRE(description.error.empty());

    const RagdollPart& upperArm = *PartOf(description, "upperarm_l");
    INFO("the shoulder sits at upperarm_l's origin and twists about the arm (down)");
    REQUIRE(Near(upperArm.jointAnchor, Vec3(0.2f, 1.55f, 0.0f), 1e-5f));
    REQUIRE(Near(Fenrir::Math::Rotate(upperArm.jointFrame.rotation, Vec3(1.0f, 0.0f, 0.0f)), Vec3(0.0f, -1.0f, 0.0f),
                 1e-5f));

    // Bend each hinge half way through its range about its axis (the frame's X), and see where the limb's far end
    // goes: forward is +Z, where the feet point.
    const auto bentEnd = [](const RagdollPart& part, const Vec3& down) {
        const HingeJoint& hinge = part.joint.As<HingeJoint>();
        const Vec3 axis = Fenrir::Math::Rotate(part.jointFrame.rotation, Vec3(1.0f, 0.0f, 0.0f));
        const float middle = 0.5f * (hinge.limits.min + hinge.limits.max);
        return Fenrir::Math::Rotate(Fenrir::Math::AngleAxis(middle, axis), down);
    };
    const RagdollPart& elbow = *PartOf(description, "lowerarm_l");
    const RagdollPart& knee = *PartOf(description, "calf_r");
    INFO("the elbow sits at lowerarm_l's origin, its axis across the arm");
    REQUIRE(Near(elbow.jointAnchor, Vec3(0.2f, 1.25f, 0.0f), 1e-5f));
    REQUIRE(Near(std::abs(Fenrir::Math::Rotate(elbow.jointFrame.rotation, Vec3(1.0f, 0.0f, 0.0f)).x), 1.0f, 1e-5f));
    INFO("bent half way, the forearm's end goes forward");
    REQUIRE(bentEnd(elbow, Vec3(0.0f, -1.0f, 0.0f)).z > 0.5f);
    INFO("bent half way, the shin's end goes backward");
    REQUIRE(bentEnd(knee, Vec3(0.0f, -1.0f, 0.0f)).z < -0.5f);
}

TEST_CASE("Offsets put each body on its bone at rest, so bone × offset is the body", "[ragdoll][describe]")
{
    RagdollTest::RagdollApp test;
    const Fenrir::Entity skeleton = test.AddSkeleton(RagdollTest::Humanoid());
    const Fenrir::SkeletonData& data = test.DataOf(skeleton);
    const RagdollDescription description = Fenrir::DescribeHumanoid(data, RagdollTest::HumanoidMap());
    REQUIRE(description.error.empty());

    // The calf's body, from the rest pose: centred between calf_l (0.1, 0.5, 0) and foot_l (0.1, 0.1, 0).
    const RagdollPart& calf = *PartOf(description, "calf_l");
    const std::int32_t bone = data.FindBone("calf_l");
    const Fenrir::Math::Mat4 model =
        *Fenrir::Skeletons::GetBaseModel(test.level, skeleton, bone) * Fenrir::TransformToMat4(calf.offset);
    INFO("calf_l's rest model matrix × its offset is the calf's body, at (0.1, 0.3, 0)");
    REQUIRE(Near(Vec3(model[3]), Vec3(0.1f, 0.3f, 0.0f), 1e-5f));
}

TEST_CASE("A map without hands and feet gives twelve parts, their mass shared out over the rest", "[ragdoll][describe]")
{
    HumanoidBones bones = RagdollTest::HumanoidMap();
    bones.leftHand = {};
    bones.rightHand = {};
    bones.leftFoot = {};
    bones.rightFoot = {};
    const RagdollDescription description = Describe(bones, 70.0f);

    INFO("the map is accepted: " << description.error);
    REQUIRE(description.error.empty());
    REQUIRE(description.parts.size() == 12);
    // Hands and feet held 2 × 0.01 + 2 × 0.015 = 0.05 of the mass: the rest share 70 kg in their fractions over 0.95.
    INFO("the chest weighs 0.22 / 0.95 of 70 kg");
    REQUIRE(Near(PartOf(description, "spine_03")->mass, 70.0f * 0.22f / 0.95f, 1e-3f));
    INFO("a calf weighs 0.045 / 0.95 of 70 kg");
    REQUIRE(Near(PartOf(description, "calf_l")->mass, 70.0f * 0.045f / 0.95f, 1e-3f));
    float total = 0.0f;
    for (const RagdollPart& part : description.parts)
    {
        total += part.mass;
    }
    INFO("the twelve still weigh 70 kg: " << total);
    REQUIRE(Near(total, 70.0f, 1e-3f));
}

TEST_CASE("An empty slot drops the chain below it", "[ragdoll][describe]")
{
    HumanoidBones bones = RagdollTest::HumanoidMap();
    bones.spine = {};
    const RagdollDescription description = Describe(bones);

    INFO("without a spine, the chest, head and arms go: the hips and two legs are left, seven parts");
    REQUIRE(description.error.empty());
    REQUIRE(description.parts.size() == 7);
    REQUIRE(PartOf(description, "spine_03") == nullptr);
    REQUIRE(PartOf(description, "upperarm_l") == nullptr);
}

TEST_CASE("A map naming an unknown bone, or not rooted at the hips, is refused with an error", "[ragdoll][describe]")
{
    HumanoidBones unknown = RagdollTest::HumanoidMap();
    unknown.head = RagdollTest::NameOf("Skull");
    const RagdollDescription refusedName = Describe(unknown);
    INFO("a name no bone has: an error naming it, and no parts");
    REQUIRE(refusedName.error.find("Skull") != std::string::npos);
    REQUIRE(refusedName.parts.empty());

    HumanoidBones near = RagdollTest::HumanoidMap();
    near.head = RagdollTest::NameOf("head");
    INFO("a name differing only in case is not guessed at");
    REQUIRE_FALSE(Describe(near).error.empty());

    HumanoidBones noHips = RagdollTest::HumanoidMap();
    noHips.hips = {};
    const RagdollDescription refusedRoot = Describe(noHips);
    INFO("no hips: refused, since every part hangs from them");
    REQUIRE_FALSE(refusedRoot.error.empty());
    REQUIRE(refusedRoot.parts.empty());

    HumanoidBones crossed = RagdollTest::HumanoidMap();
    crossed.leftLowerArm = RagdollTest::NameOf("calf_l");
    const RagdollDescription refusedTree = Describe(crossed);
    INFO("a lower arm whose bone is not below the upper arm's: the parts are not one tree under the hips");
    REQUIRE_FALSE(refusedTree.error.empty());
    REQUIRE(refusedTree.parts.empty());
}
