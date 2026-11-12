#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirSkeleton/BoneAttachment.hpp"

namespace Fenrir
{
    struct SkeletonData;

    /**
     * @brief The humanoid bone map: the bone of a rig that fills each slot, by engine name, for `DescribeHumanoid`.
     * Any slot may be empty: an empty slot drops its part and every part of the chain below it (an empty spine drops
     * the chest, head and arms). The derivation never guesses a slot from a name.
     */
    struct HumanoidBones
    {
        /** @brief The root of the map: every other slot hangs from it. */
        BoneName hips;
        /** @brief Below the hips. */
        BoneName spine;
        /** @brief Below the spine; the arms hang from it. */
        BoneName chest;
        /** @brief Below the chest. */
        BoneName head;
        /** @brief Below the chest. */
        BoneName leftUpperArm;
        /** @brief Below the left upper arm: the elbow is a hinge. */
        BoneName leftLowerArm;
        /** @brief Below the left lower arm. */
        BoneName leftHand;
        /** @brief Below the chest. */
        BoneName rightUpperArm;
        /** @brief Below the right upper arm: the elbow is a hinge. */
        BoneName rightLowerArm;
        /** @brief Below the right lower arm. */
        BoneName rightHand;
        /** @brief Below the hips. */
        BoneName leftUpperLeg;
        /** @brief Below the left upper leg: the knee is a hinge. */
        BoneName leftLowerLeg;
        /** @brief Below the left lower leg; its bone's first child gives the forward direction. */
        BoneName leftFoot;
        /** @brief Below the hips. */
        BoneName rightUpperLeg;
        /** @brief Below the right upper leg: the knee is a hinge. */
        BoneName rightLowerLeg;
        /** @brief Below the right lower leg. */
        BoneName rightFoot;
    };

    /**
     * @brief A ragdoll part: one body on one bone, and the joint joining it to its parent part's body. Everything is
     * given in the skeleton asset's rest pose, so one description serves every instance of the skeleton.
     */
    struct RagdollPart
    {
        /** @brief The bone the body follows and drives, by engine name. */
        BoneName bone;
        /** @brief The parent part's bone, an earlier part's; empty for the root part, which has no joint. */
        BoneName parent;
        /** @brief The body's collider: a capsule from `DescribeHumanoid`, any kind by hand. */
        CollisionShape shape = CapsuleShape{};
        /** @brief The body's pose relative to its bone in the rest pose: `bone⁻¹ × body`. */
        Transform offset;
        /** @brief Kilograms. */
        float mass = 1.0f;
        /** @brief The joint to the parent part's body: a hinge or swing-twist joint with limits from
         * `DescribeHumanoid`, any kind by hand. Its friction is replaced by `RagdollDescription::jointFriction` and its
         * motor turned off when built. Unused for the root part. */
        JointKind joint;
        /** @brief The joint's rest frame in model space: its rotation's X axis is the hinge's axis or the swing-twist
         * joint's twist axis. Only the rotation is read: the joint sits at `jointAnchor`. */
        JointFrame jointFrame;
        /** @brief Where the joint sits in model space at rest: the bone's origin. */
        Math::Vec3 jointAnchor = Math::Vec3(0.0f);
        /** @brief The body's `RagdollBody::strengthScale`. */
        float strengthScale = 1.0f;
    };

    /**
     * @brief A ragdoll as plain data: its parts and the ragdoll's defaults, built by `BuildRagdoll`. Derive one with
     * `DescribeHumanoid` and edit it, or fill one by hand for any skeleton. Not an asset.
     *
     * For a hand-written description: a parent part's mass and its child's within 0.8 to 1.2 of each other jitter
     * less (the physics backend's advice for ragdolls). Nothing rebalances masses for you.
     */
    struct RagdollDescription
    {
        /** @brief The parts, root first, each parent before its children. */
        std::vector<RagdollPart> parts;
        /** @brief The collision group every body gets; 0 lets the builder use the skeleton entity's id. */
        std::uint32_t group = 0;
        /** @brief Every joint's friction, newton metres: the resting stiffness that keeps a lying ragdoll still. */
        float jointFriction = 0.5f;
        /** @brief The built ragdoll's `Ragdoll::strength`. */
        float strength = 1.0f;
        /** @brief Why `DescribeHumanoid` refused, with `parts` empty; empty when the description is usable.
         * `BuildRagdoll` builds nothing from a description with an error. */
        std::string error;
    };

    /**
     * @brief Derives a humanoid ragdoll description from `skeleton`'s rest pose and `bones`.
     *
     * - **Parts:** sixteen when every slot is filled, root (hips) first, each parent before its children. An empty
     *   slot drops its part and the chain below it, and the dropped parts' mass fractions are shared out over the
     *   others in proportion.
     * - **Capsules** run from each bone to its child slot's bone; a leaf part (head, hand, foot) runs to its bone's
     *   first child in the skeleton, or on along its parent bone's direction for 0.12 (head), 0.10 (hand) or 0.13
     *   (foot) × the stature. The **stature** is the rest distance from the head's bone to a foot's (the rest pose's
     *   height when either slot is empty). Radii are fractions of the stature: hips 0.065, spine 0.055, chest 0.06,
     *   head 0.055, upper arm 0.025, lower arm 0.022, hand 0.02, upper leg 0.038, lower leg 0.03, foot 0.025; the
     *   half-height is half the length less the radius, at least 0.005 m.
     * - **Masses** are `totalMass` × hips 0.14, spine 0.12, chest 0.22, head 0.08, upper arm 0.03, lower arm 0.02,
     *   hand 0.01, upper leg 0.10, lower leg 0.045, foot 0.015 (each side's limb parts their own; they sum to 1).
     * - **Joints** sit at each part's bone origin: hinges at the elbows and knees, bending 0 to 140° (forward at the
     *   elbow, backward at the knee), and swing-twist joints elsewhere, twisting about the part, with cone half-angles
     *   and twist ranges of spine and chest 20°/±20°, head 30°/±45°, shoulder 80°/±45°, wrist 35°/±20°, hip 45°/±25°,
     *   ankle 30°/±10°. Forward is where the feet point (a foot bone's first child's horizontal direction), else +Z.
     *   Friction is `jointFriction` (0.5 N m), motors are off.
     *
     * Refuses, with an `error` and no parts, a map whose hips slot is empty, a name no bone of `skeleton` has, or a
     * filled slot whose bone is not below its parent slot's bone (the parts would not form one tree under the hips).
     */
    [[nodiscard]] RagdollDescription DescribeHumanoid(const SkeletonData& skeleton, const HumanoidBones& bones,
                                                      float totalMass = 70.0f);
} // namespace Fenrir
