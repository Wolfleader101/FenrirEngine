// DescribeHumanoid (ticket 53): a RagdollDescription from a skeleton's rest pose and a humanoid bone map. SPIKE: the
// default numbers below (radius and mass fractions, limits, friction) are what the spike settled on; the spec takes
// them from here.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirRagdoll/RagdollDescription.hpp"
#include "Frames.hpp"

namespace Fenrir
{
    namespace
    {
        using Math::Quat;
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
            std::size_t parent;
            /** @brief The slot whose bone this part's capsule runs to; NoSlot for a leaf. */
            std::size_t child;
            bool left;
        };

        // hips, spine, chest, head; left arm (upper, lower, hand), right arm; left leg (upper, lower, foot), right leg.
        constexpr std::array<Slot, SlotCount> Slots = {
            Slot{Kind::Hips, NoSlot, 1, false},  Slot{Kind::Spine, 0, 2, false},
            Slot{Kind::Chest, 1, 3, false},      Slot{Kind::Head, 2, NoSlot, false},
            Slot{Kind::UpperArm, 2, 5, true},    Slot{Kind::LowerArm, 4, 6, true},
            Slot{Kind::Hand, 5, NoSlot, true},   Slot{Kind::UpperArm, 2, 8, false},
            Slot{Kind::LowerArm, 7, 9, false},   Slot{Kind::Hand, 8, NoSlot, false},
            Slot{Kind::UpperLeg, 0, 11, true},   Slot{Kind::LowerLeg, 10, 12, true},
            Slot{Kind::Foot, 11, NoSlot, true},  Slot{Kind::UpperLeg, 0, 14, false},
            Slot{Kind::LowerLeg, 13, 15, false}, Slot{Kind::Foot, 14, NoSlot, false},
        };

        std::array<std::string_view, SlotCount> NamesOf(const HumanoidBones& bones)
        {
            return {bones.hips,          bones.spine,         bones.chest,         bones.head,
                    bones.leftUpperArm,  bones.leftLowerArm,  bones.leftHand,      bones.rightUpperArm,
                    bones.rightLowerArm, bones.rightHand,     bones.leftUpperLeg,  bones.leftLowerLeg,
                    bones.leftFoot,      bones.rightUpperLeg, bones.rightLowerLeg, bones.rightFoot};
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

        /** @brief The part's share of the total mass (both sides together sum to 1 with the torso and head). */
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

        /** @brief A leaf part's length as a fraction of the stature, when its bone has no child to run to. */
        float LeafLengthRatio(Kind kind)
        {
            switch (kind)
            {
            case Kind::Head:
                return 0.12f;
            case Kind::Hand:
                return 0.1f;
            case Kind::Foot:
                return 0.13f;
            default:
                return 0.1f;
            }
        }

        bool IsHinge(Kind kind)
        {
            return kind == Kind::LowerArm || kind == Kind::LowerLeg;
        }

        /** @brief The swing-twist limits of a part's joint to its parent, radians: cone half-angles and a twist range.
         */
        SwingTwistJoint SwingTwistOf(Kind kind, float friction)
        {
            constexpr float deg = std::numbers::pi_v<float> / 180.0f;
            SwingTwistJoint joint;
            joint.friction = friction;
            switch (kind)
            {
            case Kind::Spine:
            case Kind::Chest:
                joint.swingYLimit = joint.swingZLimit = 20.0f * deg;
                joint.twistMin = -20.0f * deg;
                joint.twistMax = 20.0f * deg;
                break;
            case Kind::Head:
                joint.swingYLimit = joint.swingZLimit = 30.0f * deg;
                joint.twistMin = -45.0f * deg;
                joint.twistMax = 45.0f * deg;
                break;
            case Kind::UpperArm:
                joint.swingYLimit = joint.swingZLimit = 80.0f * deg;
                joint.twistMin = -45.0f * deg;
                joint.twistMax = 45.0f * deg;
                break;
            case Kind::Hand:
                joint.swingYLimit = joint.swingZLimit = 35.0f * deg;
                joint.twistMin = -20.0f * deg;
                joint.twistMax = 20.0f * deg;
                break;
            case Kind::UpperLeg:
                joint.swingYLimit = joint.swingZLimit = 45.0f * deg;
                joint.twistMin = -25.0f * deg;
                joint.twistMax = 25.0f * deg;
                break;
            case Kind::Foot:
                joint.swingYLimit = joint.swingZLimit = 30.0f * deg;
                joint.twistMin = -10.0f * deg;
                joint.twistMax = 10.0f * deg;
                break;
            default:
                break;
            }
            return joint;
        }

        /** @brief The elbow's and knee's bend: 0 to 140° one way from the rest pose. */
        constexpr float HingeBend = 140.0f * std::numbers::pi_v<float> / 180.0f;

        Vec3 Horizontal(const Vec3& v)
        {
            return Vec3(v.x, 0.0f, v.z);
        }

        /** @brief For BoneName::Set on names already checked against the skeleton: nothing to log. */
        class SilentLogger final : public ILogger
        {
          public:
            void AddLogger(std::shared_ptr<ILogger>) override
            {
            }
            void RemoveLogger(std::shared_ptr<ILogger>) override
            {
            }

          protected:
            void LogImpl(std::string_view) override
            {
            }
            void InfoImpl(std::string_view) override
            {
            }
            void WarnImpl(std::string_view) override
            {
            }
            void ErrorImpl(std::string_view) override
            {
            }
            void FatalImpl(std::string_view) override
            {
            }
        };
    } // namespace

    RagdollDescription DescribeHumanoid(const SkeletonData& skeleton, const HumanoidBones& bones, float totalMass)
    {
        RagdollDescription description;
        const auto names = NamesOf(bones);

        // Resolve every filled slot, refusing a name no bone has; drop the chain below an empty slot.
        std::array<std::int32_t, SlotCount> bone{};
        std::array<bool, SlotCount> filled{};
        for (std::size_t i = 0; i < SlotCount; ++i)
        {
            bone[i] = NoBone;
            if (names[i].empty())
            {
                continue;
            }
            bone[i] = skeleton.FindBone(names[i]);
            if (bone[i] == NoBone)
            {
                description.error = "DescribeHumanoid: no bone is named '" + std::string(names[i]) + "'";
                return description;
            }
            filled[i] = true;
        }
        if (!filled[0])
        {
            description.error = "DescribeHumanoid: the hips slot is empty, and every part hangs from it";
            return description;
        }
        for (std::size_t i = 1; i < SlotCount; ++i)
        {
            if (filled[i] && !filled[Slots[i].parent])
            {
                filled[i] = false; // its chain's parent was dropped: drop it too
            }
            if (filled[i] && !skeleton.IsDescendant(bone[i], bone[Slots[i].parent]))
            {
                description.error = "DescribeHumanoid: '" + std::string(names[i]) + "' is not below '" +
                                    std::string(names[Slots[i].parent]) +
                                    "' in the skeleton, so the parts are not one tree";
                return description;
            }
        }

        const std::vector<Detail::RestPose> rest = Detail::RestPoses(skeleton);
        const auto position = [&](std::int32_t b) { return rest[static_cast<std::size_t>(b)].pos; };

        // The stature: head to foot in the rest pose, else a human's 1.8 m.
        float stature = 1.8f;
        if (filled[3] && filled[12])
        {
            stature = std::max(0.2f, Math::Magnitude(position(bone[3]) - position(bone[12])));
        }
        // Forward is where the feet point (a foot bone's first child), else +Z (glTF's front).
        Vec3 forward(0.0f, 0.0f, 1.0f);
        for (const std::size_t foot : {std::size_t{12}, std::size_t{15}})
        {
            if (!filled[foot])
            {
                continue;
            }
            const std::vector<std::int32_t> below = skeleton.Subtree(bone[foot]);
            if (below.size() > 1)
            {
                const Vec3 toe = Horizontal(position(below[1]) - position(bone[foot]));
                if (Math::Magnitude(toe) > 1e-4f)
                {
                    forward = Math::Normalized(toe);
                    break;
                }
            }
        }

        // The direction and length each part runs: to its child slot's bone, else a leaf's first child bone, else on
        // along its own parent bone's direction for a default length.
        std::array<Vec3, SlotCount> direction{};
        std::array<float, SlotCount> length{};
        for (std::size_t i = 0; i < SlotCount; ++i)
        {
            if (!filled[i])
            {
                continue;
            }
            const Slot& slot = Slots[i];
            const Vec3 from = position(bone[i]);
            Vec3 run(0.0f);
            if (slot.child != NoSlot && filled[slot.child])
            {
                run = position(bone[slot.child]) - from;
            }
            else
            {
                const std::vector<std::int32_t> below = skeleton.Subtree(bone[i]);
                if (below.size() > 1)
                {
                    run = position(below[1]) - from;
                }
                if (Math::Magnitude(run) < 1e-4f)
                {
                    const std::int32_t parentBone = skeleton.Parent(bone[i]);
                    const Vec3 along = parentBone == NoBone ? Vec3(0.0f, 1.0f, 0.0f) : from - position(parentBone);
                    run = (Math::Magnitude(along) > 1e-4f ? Math::Normalized(along) : Vec3(0.0f, 1.0f, 0.0f)) *
                          (LeafLengthRatio(slot.kind) * stature);
                }
            }
            length[i] = Math::Magnitude(run);
            direction[i] = length[i] > 1e-4f ? run / length[i] : Vec3(0.0f, 1.0f, 0.0f);
            if (length[i] <= 1e-4f)
            {
                length[i] = 2.0f * RadiusRatio(slot.kind) * stature;
            }
        }

        description.parts.reserve(SlotCount);
        for (std::size_t i = 0; i < SlotCount; ++i)
        {
            if (!filled[i])
            {
                continue;
            }
            const Slot& slot = Slots[i];
            const Detail::RestPose& boneRest = rest[static_cast<std::size_t>(bone[i])];
            RagdollPart part;
            // Names were found above, so Set cannot refuse them: the silent logger is never reached.
            SilentLogger silent;
            part.bone.Set(names[i], silent);
            if (slot.parent != NoSlot)
            {
                part.parent.Set(names[slot.parent], silent);
            }

            // A capsule along the run, centred on it, its Y the run's direction.
            const float radius = RadiusRatio(slot.kind) * stature;
            const float halfHeight = std::max(0.5f * length[i] - radius, 0.005f);
            part.shape = CapsuleShape{.halfHeight = halfHeight, .radius = radius};
            const Detail::RestPose body{boneRest.pos + direction[i] * (0.5f * length[i]),
                                        Detail::FromTo(Vec3(0.0f, 1.0f, 0.0f), direction[i])};
            // offset = bone⁻¹ × body.
            const Quat inverseBone = Math::Conjugate(boneRest.rot);
            part.offset.pos = Math::Rotate(inverseBone, body.pos - boneRest.pos);
            part.offset.rot = Math::Normalized(inverseBone * body.rot);
            part.mass = MassFraction(slot.kind) * totalMass;

            part.jointAnchor = boneRest.pos;
            if (slot.parent != NoSlot)
            {
                if (IsHinge(slot.kind))
                {
                    // The bend axis: across the limb and across forward. The elbow bends the forearm's far end forward,
                    // the knee the shin's backward; the hinge's sign follows the axis, so the range is put on that
                    // side.
                    Vec3 axis = Math::Cross(forward, direction[i]);
                    if (Math::Magnitude(axis) < 1e-3f)
                    {
                        axis = Math::Cross(Vec3(0.0f, 1.0f, 0.0f), direction[i]);
                    }
                    axis = Math::Normalized(axis);
                    const Vec3 bent = Math::Rotate(Math::AngleAxis(0.1f, axis), direction[i]) - direction[i];
                    const bool positiveIsForward = Math::Dot(bent, forward) > 0.0f;
                    const bool bendForward = slot.kind == Kind::LowerArm;
                    HingeJoint hinge;
                    hinge.friction = description.jointFriction;
                    hinge.limits.enabled = true;
                    if (positiveIsForward == bendForward)
                    {
                        hinge.limits.min = 0.0f;
                        hinge.limits.max = HingeBend;
                    }
                    else
                    {
                        hinge.limits.min = -HingeBend;
                        hinge.limits.max = 0.0f;
                    }
                    part.joint = hinge;
                    part.jointFrame = Detail::BasisWithX(axis, direction[i]);
                }
                else
                {
                    part.joint = SwingTwistOf(slot.kind, description.jointFriction);
                    // The twist axis along the part, the cone about it.
                    part.jointFrame = Detail::BasisWithX(direction[i], forward);
                }
            }
            description.parts.push_back(part);
        }
        return description;
    }
} // namespace Fenrir
