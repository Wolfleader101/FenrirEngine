// DescribeHumanoid: a RagdollDescription from a skeleton's rest pose and a humanoid bone map, with the spec's numbers
// (the ragdoll spike's, user-checked on the Quaternius human).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirRagdoll/Description.hpp"
#include "Frames.hpp"

namespace Fenrir
{
    namespace
    {
        using Math::Vec3;

        constexpr std::size_t SlotCount = 16;
        constexpr std::size_t NoSlot = SlotCount;

        /** @brief What a slot's part is: how its capsule is sized, weighed and jointed. */
        enum class Kind : std::uint8_t
        {
            Hips,
            Spine,
            Chest,
            Head,
            UpperArm,
            LowerArm,
            Hand,
            UpperLeg,
            LowerLeg,
            Foot,
        };

        struct Slot
        {
            Kind kind;
            /** @brief The slot this one hangs from; NoSlot for the hips. */
            std::size_t parent;
            /** @brief The slot whose bone this part's capsule runs to; NoSlot for a leaf. */
            std::size_t child;
        };

        // In HumanoidBones' order: hips, spine, chest, head; the left arm (upper, lower, hand), the right arm; the left
        // leg (upper, lower, foot), the right leg. Each slot comes after the one it hangs from.
        constexpr std::array<Slot, SlotCount> Slots = {
            Slot{Kind::Hips, NoSlot, 1},  Slot{Kind::Spine, 0, 2},     Slot{Kind::Chest, 1, 3},
            Slot{Kind::Head, 2, NoSlot},  Slot{Kind::UpperArm, 2, 5},  Slot{Kind::LowerArm, 4, 6},
            Slot{Kind::Hand, 5, NoSlot},  Slot{Kind::UpperArm, 2, 8},  Slot{Kind::LowerArm, 7, 9},
            Slot{Kind::Hand, 8, NoSlot},  Slot{Kind::UpperLeg, 0, 11}, Slot{Kind::LowerLeg, 10, 12},
            Slot{Kind::Foot, 11, NoSlot}, Slot{Kind::UpperLeg, 0, 14}, Slot{Kind::LowerLeg, 13, 15},
            Slot{Kind::Foot, 14, NoSlot},
        };
        constexpr std::size_t HeadSlot = 3;
        constexpr std::array<std::size_t, 2> FootSlots = {12, 15};

        std::array<const BoneName*, SlotCount> NamesOf(const HumanoidBones& bones)
        {
            return {&bones.hips,          &bones.spine,         &bones.chest,         &bones.head,
                    &bones.leftUpperArm,  &bones.leftLowerArm,  &bones.leftHand,      &bones.rightUpperArm,
                    &bones.rightLowerArm, &bones.rightHand,     &bones.leftUpperLeg,  &bones.leftLowerLeg,
                    &bones.leftFoot,      &bones.rightUpperLeg, &bones.rightLowerLeg, &bones.rightFoot};
        }

        /** @brief The capsule's radius as a fraction of the stature. */
        float RadiusRatio(Kind kind)
        {
            switch (kind)
            {
            case Kind::Hips:
                return 0.065f;
            case Kind::Spine:
                return 0.055f;
            case Kind::Chest:
                return 0.06f;
            case Kind::Head:
                return 0.055f;
            case Kind::UpperArm:
                return 0.025f;
            case Kind::LowerArm:
                return 0.022f;
            case Kind::Hand:
                return 0.02f;
            case Kind::UpperLeg:
                return 0.038f;
            case Kind::LowerLeg:
                return 0.03f;
            case Kind::Foot:
                return 0.025f;
            }
            return 0.03f;
        }

        /** @brief The part's share of the total mass; every slot's together sum to 1. */
        float MassFraction(Kind kind)
        {
            switch (kind)
            {
            case Kind::Hips:
                return 0.14f;
            case Kind::Spine:
                return 0.12f;
            case Kind::Chest:
                return 0.22f;
            case Kind::Head:
                return 0.08f;
            case Kind::UpperArm:
                return 0.03f;
            case Kind::LowerArm:
                return 0.02f;
            case Kind::Hand:
                return 0.01f;
            case Kind::UpperLeg:
                return 0.10f;
            case Kind::LowerLeg:
                return 0.045f;
            case Kind::Foot:
                return 0.015f;
            }
            return 0.0f;
        }

        /** @brief How long a part with no bone to run to is, as a fraction of the stature. */
        float LeafLengthRatio(Kind kind)
        {
            switch (kind)
            {
            case Kind::Head:
                return 0.12f;
            case Kind::Foot:
                return 0.13f;
            default:
                return 0.10f;
            }
        }

        constexpr float Degree = std::numbers::pi_v<float> / 180.0f;
        /** @brief How far an elbow or knee bends from the rest pose. */
        constexpr float HingeBend = 140.0f * Degree;

        /** @brief A swing-twist joint with the slot's cone half-angle and twist either way, in degrees. */
        SwingTwistJoint SwingTwistOf(Kind kind, float friction)
        {
            float cone = 20.0f;
            float twist = 20.0f;
            switch (kind)
            {
            case Kind::Head:
                cone = 30.0f;
                twist = 45.0f;
                break;
            case Kind::UpperArm:
                cone = 80.0f;
                twist = 45.0f;
                break;
            case Kind::Hand:
                cone = 35.0f;
                twist = 20.0f;
                break;
            case Kind::UpperLeg:
                cone = 45.0f;
                twist = 25.0f;
                break;
            case Kind::Foot:
                cone = 30.0f;
                twist = 10.0f;
                break;
            default: // the spine and chest
                break;
            }
            SwingTwistJoint joint;
            joint.swingYLimit = cone * Degree;
            joint.swingZLimit = cone * Degree;
            joint.twistMin = -twist * Degree;
            joint.twistMax = twist * Degree;
            joint.friction = friction;
            return joint;
        }

        RagdollDescription Refused(std::string error)
        {
            RagdollDescription refused;
            refused.error = "DescribeHumanoid: " + std::move(error);
            return refused;
        }
    } // namespace

    RagdollDescription DescribeHumanoid(const SkeletonData& skeleton, const HumanoidBones& bones, float totalMass)
    {
        const auto names = NamesOf(bones);

        // Each filled slot's bone, refusing a name no bone has; a slot under a dropped one is dropped too.
        std::array<std::int32_t, SlotCount> bone{};
        std::array<bool, SlotCount> filled{};
        for (std::size_t i = 0; i < SlotCount; ++i)
        {
            bone[i] = NoBone;
            const std::optional<std::string_view> name = names[i]->Get();
            if (!name)
            {
                return Refused("a slot's bone name has no terminator");
            }
            if (name->empty())
            {
                continue;
            }
            bone[i] = skeleton.FindBone(*name);
            if (bone[i] == NoBone)
            {
                return Refused("no bone is named '" + std::string(*name) + "'");
            }
            filled[i] = true;
        }
        if (!filled[0])
        {
            return Refused("the hips slot is empty, and every part hangs from it");
        }
        for (std::size_t i = 1; i < SlotCount; ++i)
        {
            const std::size_t parent = Slots[i].parent;
            filled[i] = filled[i] && filled[parent];
            if (filled[i] && !skeleton.IsDescendant(bone[i], bone[parent]))
            {
                return Refused("'" + std::string(*names[i]->Get()) + "' is not below '" +
                               std::string(*names[parent]->Get()) + "', so the parts are not one tree under the hips");
            }
        }

        const std::vector<Detail::Pose> rest = Detail::RestPoses(skeleton);
        const auto at = [&](std::int32_t b) { return rest[static_cast<std::size_t>(b)].pos; };
        const auto firstChild = [&](std::int32_t b) {
            const std::vector<std::int32_t> below = skeleton.Subtree(b);
            return below.size() > 1 ? below[1] : NoBone;
        };

        // The stature: the head's bone to a foot's, else the rest pose's height.
        float stature = 0.0f;
        for (const std::size_t foot : FootSlots)
        {
            if (stature == 0.0f && filled[HeadSlot] && filled[foot])
            {
                stature = Math::Magnitude(at(bone[HeadSlot]) - at(bone[foot]));
            }
        }
        if (stature == 0.0f && !rest.empty())
        {
            const auto [low, high] = std::ranges::minmax_element(
                rest, [](const Detail::Pose& a, const Detail::Pose& b) { return a.pos.y < b.pos.y; });
            stature = high->pos.y - low->pos.y;
        }
        stature = std::max(stature, 0.2f);

        // Forward is where the feet point, else +Z.
        Vec3 forward(0.0f, 0.0f, 1.0f);
        for (auto it = FootSlots.rbegin(); it != FootSlots.rend(); ++it)
        {
            const std::size_t foot = *it;
            const std::int32_t toe = filled[foot] ? firstChild(bone[foot]) : NoBone;
            if (toe != NoBone)
            {
                Vec3 along = at(toe) - at(bone[foot]);
                along.y = 0.0f;
                forward = Math::Magnitude(along) > 1e-4f ? Math::Normalized(along) : forward;
            }
        }

        float massShare = 0.0f;
        for (std::size_t i = 0; i < SlotCount; ++i)
        {
            massShare += filled[i] ? MassFraction(Slots[i].kind) : 0.0f;
        }

        RagdollDescription description;
        description.parts.reserve(SlotCount);
        for (std::size_t i = 0; i < SlotCount; ++i)
        {
            if (!filled[i])
            {
                continue;
            }
            const Slot& slot = Slots[i];
            const Detail::Pose& boneRest = rest[static_cast<std::size_t>(bone[i])];

            // The run: to the child slot's bone, else to the bone's first child, else on along the parent bone.
            Vec3 run(0.0f);
            if (slot.child != NoSlot && filled[slot.child])
            {
                run = at(bone[slot.child]) - boneRest.pos;
            }
            else if (const std::int32_t child = firstChild(bone[i]); child != NoBone)
            {
                run = at(child) - boneRest.pos;
            }
            if (Math::Magnitude(run) < 1e-4f)
            {
                const std::int32_t parentBone = skeleton.Parent(bone[i]);
                const Vec3 along = parentBone == NoBone ? Vec3(0.0f) : boneRest.pos - at(parentBone);
                run = (Math::Magnitude(along) > 1e-4f ? Math::Normalized(along) : Vec3(0.0f, 1.0f, 0.0f)) *
                      (LeafLengthRatio(slot.kind) * stature);
            }
            const float length = Math::Magnitude(run);
            const Vec3 direction = run / length;

            RagdollPart part;
            part.bone = *names[i];
            if (slot.parent != NoSlot)
            {
                part.parent = *names[slot.parent];
            }
            const float radius = RadiusRatio(slot.kind) * stature;
            part.shape = CapsuleShape{.halfHeight = std::max(0.5f * length - radius, 0.005f), .radius = radius};
            // The capsule stands along the body's Y, centred on the run; offset = bone⁻¹ × body.
            const Detail::Pose body{boneRest.pos + direction * (0.5f * length),
                                    Detail::FromTo(Vec3(0.0f, 1.0f, 0.0f), direction)};
            const Math::Quat inverseBone = Math::Conjugate(boneRest.rot);
            part.offset.pos = Math::Rotate(inverseBone, body.pos - boneRest.pos);
            part.offset.rot = Math::Normalized(inverseBone * body.rot);
            part.mass = MassFraction(slot.kind) / massShare * totalMass;
            part.jointAnchor = boneRest.pos;
            part.jointFrame.position = boneRest.pos;

            if (slot.parent != NoSlot && (slot.kind == Kind::LowerArm || slot.kind == Kind::LowerLeg))
            {
                // The bend axis lies across the limb and across forward. An elbow bends the forearm's end forward, a
                // knee the shin's backward: the hinge's angle is signed about the axis, so the range goes on the side
                // a small turn about it moves the end towards.
                Vec3 axis = Math::Cross(forward, direction);
                if (Math::Magnitude(axis) < 1e-3f)
                {
                    axis = Math::Cross(Vec3(0.0f, 1.0f, 0.0f), direction);
                }
                axis = Math::Normalized(axis);
                const Vec3 bent = Math::Rotate(Math::AngleAxis(0.1f, axis), direction) - direction;
                const bool positiveGoesForward = Math::Dot(bent, forward) > 0.0f;
                const bool bendsForward = slot.kind == Kind::LowerArm;
                HingeJoint hinge;
                hinge.friction = description.jointFriction;
                hinge.limits.enabled = true;
                hinge.limits.min = positiveGoesForward == bendsForward ? 0.0f : -HingeBend;
                hinge.limits.max = positiveGoesForward == bendsForward ? HingeBend : 0.0f;
                part.joint = hinge;
                part.jointFrame.rotation = Detail::BasisWithX(axis, direction);
            }
            else if (slot.parent != NoSlot)
            {
                part.joint = SwingTwistOf(slot.kind, description.jointFriction);
                // The twist axis runs along the part, the cone about it.
                part.jointFrame.rotation = Detail::BasisWithX(direction, forward);
            }
            description.parts.push_back(part);
        }
        return description;
    }
} // namespace Fenrir
