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

        /** @brief A kind's numbers from the spec. */
        struct KindNumbers
        {
            /** @brief The capsule's radius, × the stature. */
            float radius;
            /** @brief Its share of the total mass; every slot's together sum to 1. */
            float mass;
            /** @brief Its length when its bone has nothing to run to, × the stature. */
            float leafLength;
            /** @brief Whether the joint to its parent is a hinge (elbow, knee); else a swing-twist joint. */
            bool hinge;
            /** @brief A swing-twist joint's cone half-angle and twist either way, degrees. */
            float cone;
            float twist;
        };

        // Indexed by Kind. The hips have no joint.
        constexpr std::array<KindNumbers, 10> Numbers = {
            KindNumbers{0.065f, 0.14f, 0.10f, false, 0.0f, 0.0f},    // Hips
            KindNumbers{0.055f, 0.12f, 0.10f, false, 20.0f, 20.0f},  // Spine
            KindNumbers{0.06f, 0.22f, 0.10f, false, 20.0f, 20.0f},   // Chest
            KindNumbers{0.055f, 0.08f, 0.12f, false, 30.0f, 45.0f},  // Head
            KindNumbers{0.025f, 0.03f, 0.10f, false, 80.0f, 45.0f},  // UpperArm (the shoulder)
            KindNumbers{0.022f, 0.02f, 0.10f, true, 0.0f, 0.0f},     // LowerArm (the elbow)
            KindNumbers{0.02f, 0.01f, 0.10f, false, 35.0f, 20.0f},   // Hand (the wrist)
            KindNumbers{0.038f, 0.10f, 0.10f, false, 45.0f, 25.0f},  // UpperLeg (the hip)
            KindNumbers{0.03f, 0.045f, 0.10f, true, 0.0f, 0.0f},     // LowerLeg (the knee)
            KindNumbers{0.025f, 0.015f, 0.13f, false, 30.0f, 10.0f}, // Foot (the ankle)
        };

        const KindNumbers& NumbersOf(Kind kind)
        {
            return Numbers[static_cast<std::size_t>(kind)];
        }

        struct Slot
        {
            Kind kind;
            /** @brief The slot this one hangs from; NoSlot for the hips. */
            std::size_t parent;
            /** @brief The slot whose bone this part's capsule runs to; NoSlot for a leaf. */
            std::size_t child;
            /** @brief Whether its hinge bends the limb's end forward (an elbow) rather than back (a knee). */
            bool bendsForward;
        };

        // In HumanoidBones' order: hips, spine, chest, head; the left arm (upper, lower, hand), the right arm; the left
        // leg (upper, lower, foot), the right leg. Each slot comes after the one it hangs from.
        constexpr std::array<Slot, SlotCount> Slots = {
            Slot{Kind::Hips, NoSlot, 1, false},  Slot{Kind::Spine, 0, 2, false},
            Slot{Kind::Chest, 1, 3, false},      Slot{Kind::Head, 2, NoSlot, false},
            Slot{Kind::UpperArm, 2, 5, false},   Slot{Kind::LowerArm, 4, 6, true},
            Slot{Kind::Hand, 5, NoSlot, false},  Slot{Kind::UpperArm, 2, 8, false},
            Slot{Kind::LowerArm, 7, 9, true},    Slot{Kind::Hand, 8, NoSlot, false},
            Slot{Kind::UpperLeg, 0, 11, false},  Slot{Kind::LowerLeg, 10, 12, false},
            Slot{Kind::Foot, 11, NoSlot, false}, Slot{Kind::UpperLeg, 0, 14, false},
            Slot{Kind::LowerLeg, 13, 15, false}, Slot{Kind::Foot, 14, NoSlot, false},
        };
        constexpr std::size_t HeadSlot = 3;
        constexpr std::array<std::size_t, 2> FootSlots = {12, 15};

        constexpr float Degree = std::numbers::pi_v<float> / 180.0f;
        /** @brief How far an elbow or knee bends from the rest pose. */
        constexpr float HingeBend = 140.0f * Degree;

        std::array<const BoneName*, SlotCount> NamesOf(const HumanoidBones& bones)
        {
            return {&bones.hips,          &bones.spine,         &bones.chest,         &bones.head,
                    &bones.leftUpperArm,  &bones.leftLowerArm,  &bones.leftHand,      &bones.rightUpperArm,
                    &bones.rightLowerArm, &bones.rightHand,     &bones.leftUpperLeg,  &bones.leftLowerLeg,
                    &bones.leftFoot,      &bones.rightUpperLeg, &bones.rightLowerLeg, &bones.rightFoot};
        }

        RagdollDescription Refused(std::string error)
        {
            RagdollDescription refused;
            refused.error = "DescribeHumanoid: " + std::move(error);
            return refused;
        }

        /** @brief A hinge whose 140° range lies on the side a turn about `axis` must take to move the limb's end
         * (along `direction`) forward or back, as `bendsForward` says. The hinge's angle is signed about its axis. */
        HingeJoint HingeOf(const Vec3& axis, const Vec3& direction, const Vec3& forward, bool bendsForward,
                           float friction)
        {
            const Vec3 bent = Math::Rotate(Math::AngleAxis(0.1f, axis), direction) - direction;
            const bool positiveGoesForward = Math::Dot(bent, forward) > 0.0f;
            HingeJoint hinge;
            hinge.friction = friction;
            hinge.limits.enabled = true;
            hinge.limits.min = positiveGoesForward == bendsForward ? 0.0f : -HingeBend;
            hinge.limits.max = positiveGoesForward == bendsForward ? HingeBend : 0.0f;
            return hinge;
        }

        SwingTwistJoint SwingTwistOf(const KindNumbers& numbers, float friction)
        {
            SwingTwistJoint joint;
            joint.swingYLimit = numbers.cone * Degree;
            joint.swingZLimit = numbers.cone * Degree;
            joint.twistMin = -numbers.twist * Degree;
            joint.twistMax = numbers.twist * Degree;
            joint.friction = friction;
            return joint;
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

        const std::vector<Math::Mat4> rest = Detail::RestModels(skeleton);
        const auto at = [&](std::int32_t b) { return Vec3(rest[static_cast<std::size_t>(b)][3]); };
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
                rest, [](const Math::Mat4& a, const Math::Mat4& b) { return a[3].y < b[3].y; });
            stature = (*high)[3].y - (*low)[3].y;
        }
        stature = std::max(stature, 0.2f);

        // Forward is where the feet point (the left foot's when both are mapped), else +Z.
        Vec3 forward(0.0f, 0.0f, 1.0f);
        for (auto it = FootSlots.rbegin(); it != FootSlots.rend(); ++it)
        {
            const std::int32_t toe = filled[*it] ? firstChild(bone[*it]) : NoBone;
            if (toe != NoBone)
            {
                Vec3 along = at(toe) - at(bone[*it]);
                along.y = 0.0f;
                forward = Math::Magnitude(along) > 1e-4f ? Math::Normalized(along) : forward;
            }
        }

        float massShare = 0.0f;
        for (std::size_t i = 0; i < SlotCount; ++i)
        {
            massShare += filled[i] ? NumbersOf(Slots[i].kind).mass : 0.0f;
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
            const KindNumbers& numbers = NumbersOf(slot.kind);
            const Math::Mat4& boneModel = rest[static_cast<std::size_t>(bone[i])];
            const Vec3 origin = at(bone[i]);

            // The run: to the child slot's bone, else to the bone's first child, else on along the parent bone.
            Vec3 run(0.0f);
            if (slot.child != NoSlot && filled[slot.child])
            {
                run = at(bone[slot.child]) - origin;
            }
            else if (const std::int32_t child = firstChild(bone[i]); child != NoBone)
            {
                run = at(child) - origin;
            }
            if (Math::Magnitude(run) < 1e-4f)
            {
                const std::int32_t parentBone = skeleton.Parent(bone[i]);
                const Vec3 along = parentBone == NoBone ? Vec3(0.0f) : origin - at(parentBone);
                run = (Math::Magnitude(along) > 1e-4f ? Math::Normalized(along) : Vec3(0.0f, 1.0f, 0.0f)) *
                      (numbers.leafLength * stature);
            }
            const float length = Math::Magnitude(run);
            const Vec3 direction = run / length;

            RagdollPart part;
            part.bone = *names[i];
            if (slot.parent != NoSlot)
            {
                part.parent = *names[slot.parent];
            }
            const float radius = numbers.radius * stature;
            part.shape = CapsuleShape{.halfHeight = std::max(0.5f * length - radius, 0.005f), .radius = radius};
            // The capsule stands along the body's Y, centred on the run; offset = bone⁻¹ × body, through the full
            // matrices, so a scaled armature's bone × offset is the body again.
            const Math::Mat4 body = Math::Translate(Math::Mat4(1.0f), origin + direction * (0.5f * length)) *
                                    Math::QuatToMat4(Detail::FromTo(Vec3(0.0f, 1.0f, 0.0f), direction));
            Math::Decompose(Math::Inverse(boneModel) * body, part.offset.pos, part.offset.rot, part.offset.scale);
            part.mass = numbers.mass / massShare * totalMass;
            part.jointAnchor = origin;
            part.jointFrame.position = origin;

            if (slot.parent != NoSlot && numbers.hinge)
            {
                // The bend axis lies across the limb and across forward.
                Vec3 axis = Math::Cross(forward, direction);
                if (Math::Magnitude(axis) < 1e-3f)
                {
                    axis = Math::Cross(Vec3(0.0f, 1.0f, 0.0f), direction);
                }
                axis = Math::Normalized(axis);
                part.joint = HingeOf(axis, direction, forward, slot.bendsForward, description.jointFriction);
                part.jointFrame.rotation = Detail::BasisWithX(axis, direction);
            }
            else if (slot.parent != NoSlot)
            {
                part.joint = SwingTwistOf(numbers, description.jointFriction);
                // The twist axis runs along the part, the cone about it.
                part.jointFrame.rotation = Detail::BasisWithX(direction, forward);
            }
            description.parts.push_back(part);
        }
        return description;
    }
} // namespace Fenrir
