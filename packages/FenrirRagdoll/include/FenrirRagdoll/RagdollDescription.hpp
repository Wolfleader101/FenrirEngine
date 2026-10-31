#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirSkeleton/BoneAttachment.hpp"

namespace Fenrir
{
    class App;
    class Scene;
    struct SkeletonData;

    /**
     * @brief The humanoid bone map (ticket 53): which bone of a rig fills each slot, by engine name. An empty slot
     * drops its part and the chain below it. Game code fills it per rig; the derivation never guesses by substring.
     */
    struct HumanoidBones
    {
        std::string hips;
        std::string spine;
        std::string chest;
        std::string head;
        std::string leftUpperArm, leftLowerArm, leftHand;
        std::string rightUpperArm, rightLowerArm, rightHand;
        std::string leftUpperLeg, leftLowerLeg, leftFoot;
        std::string rightUpperLeg, rightLowerLeg, rightFoot;
    };

    /**
     * @brief One ragdoll part (ticket 53): a body on a bone, joined to its parent part's body at this bone's origin.
     * `offset` and `jointFrame` are in the rest pose: the body's pose relative to its bone, and the joint's frame in
     * model space (its X the hinge axis or the twist axis), which the builder takes into both bodies' spaces.
     */
    struct RagdollPart
    {
        BoneName bone;
        /** @brief The parent part's bone; empty for the root part. */
        BoneName parent;
        CollisionShape shape;
        Transform offset;
        float mass = 1.0f;
        JointKind joint;
        float strengthScale = 1.0f;
        /** @brief SPIKE: the joint's rest frame in model space; the description decision left it implicit. */
        Math::Quat jointFrame = Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
        /** @brief Where in model space the joint sits at rest: the bone's origin. */
        Math::Vec3 jointAnchor = Math::Vec3(0.0f);
    };

    /** @brief A ragdoll as plain data: its parts, root first, and the ragdoll's defaults. */
    struct RagdollDescription
    {
        std::vector<RagdollPart> parts;
        /** @brief The collision group the bodies share; 0 lets the builder use the skeleton entity's id. */
        std::uint32_t group = 0;
        /** @brief The joint friction every joint starts with, as resting stiffness, in newton metres. */
        float jointFriction = 0.5f;
        /** @brief The ragdoll's initial strength. */
        float strength = 0.5f;
        /** @brief Why a derivation refused, empty when `parts` are usable. */
        std::string error;
    };

    /**
     * @brief Derives a ragdoll description from a skeleton's rest pose and a humanoid bone map (ticket 53): a capsule
     * per filled slot from the bone to its child in the map (a leaf slot runs to the bone's first child in the
     * skeleton, or on along its parent's direction), radii as fractions of the stature, mass from `totalMass` by fixed
     * per-part fractions, hinges at elbows and knees, swing-twist elsewhere, each with its slot's default limits.
     * Refuses (an `error`, no parts) a map whose filled slots do not form one tree rooted at the hips, or a name no
     * bone has.
     */
    [[nodiscard]] RagdollDescription DescribeHumanoid(const SkeletonData& skeleton, const HumanoidBones& bones,
                                                      float totalMass = 70.0f);

    /** @brief What BuildRagdoll spawned, for game code to edit or DestroyRagdoll to remove. */
    struct SpawnedRagdoll
    {
        /** @brief The holder the bodies and joints sit under, at the scene's root. */
        Entity parent;
        /** @brief One per part, in the description's order. */
        std::vector<Entity> bodies;
        /** @brief One per non-root part, in the description's order (the root has none). */
        std::vector<Entity> joints;
        /** @brief Why nothing was built, empty on success. */
        std::string error;
    };

    /**
     * @brief Spawns `description` for `skeleton` (ticket 53): one body entity per part (RigidBody kinematic,
     * Collider, RagdollBody, CollisionGroup) at the rest pose under the skeleton entity's world transform, one Joint
     * entity per non-root part with frames from FrameFromWorld, root first, under a holder entity; puts Ragdoll on
     * the skeleton entity in following mode. `app` reaches the asset store for the skeleton's rest pose.
     */
    SpawnedRagdoll BuildRagdoll(App& app, Scene& scene, Entity skeleton, const RagdollDescription& description);

    /** @brief Destroys what BuildRagdoll spawned, and removes the Ragdoll from `skeleton` if it is alive. */
    void DestroyRagdoll(Scene& scene, Entity skeleton, const SpawnedRagdoll& spawned);
} // namespace Fenrir
