#pragma once

#include <cstdint>

#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirSkeleton/BoneAttachment.hpp"

namespace Fenrir
{
    /** @brief How a ragdoll's bodies move (ADR-0054). Game code writes it; the ragdoll glue applies it each tick. */
    enum class RagdollMode : std::uint8_t
    {
        /** @brief Every body kinematic, moved each tick to its bone in the simulation pose × its offset: the
         * character's hit boxes. */
        Following,
        /** @brief Every body dynamic, the glue writing each body's pose into the skeleton as a world pose override. */
        Simulating,
        /** @brief Dynamic, with each joint's motor driving towards the base pose at `Ragdoll::strength`. */
        Powered,
    };

    /** @brief A ragdoll body's own mode: its ragdoll's (`Inherit`), or one of the three, so a subtree of bodies can
     * differ from its ragdoll (a partial ragdoll, ADR-0054). */
    enum class RagdollBodyMode : std::uint8_t
    {
        /** @brief The ragdoll's `mode`. */
        Inherit,
        /** @brief Following, whatever the ragdoll's mode. */
        Following,
        /** @brief Simulating, whatever the ragdoll's mode. */
        Simulating,
        /** @brief Powered, whatever the ragdoll's mode. */
        Powered,
    };

    /** @brief What the ragdoll glue found last tick. Glue-owned: a write to it is overwritten next tick. */
    struct RagdollState
    {
        /** @brief Whether any body the glue drives is awake. */
        bool anyAwake = false;
        /** @brief How many bodies the glue drives this tick: its ragdoll bodies that were not skipped. */
        std::int32_t bodies = 0;
        /** @brief Whether the ragdoll is inactive: its entity has no ready, tick-posed `SkeletonPose`. */
        bool inactive = false;
    };

    /**
     * @brief A ragdoll (ADR-0053): on the skeleton entity (the `SkeletonPose` entity), so a skeleton has at most one.
     * Its bodies are ordinary rigid body and collider entities, each with a `RagdollBody` naming this entity, joined by
     * ordinary `Joint` entities. `BuildRagdoll` makes them and this component; game code writes `mode` (and each
     * body's own `mode`), the ragdoll glue turns it into motion types and motors each tick (ADR-0054).
     *
     * A ragdoll needs a tick-posed skeleton (`SkeletonPose::timing == PoseTiming::Tick`, ADR-0040), and the glue never
     * sets it: on a frame-posed or not-ready skeleton the ragdoll is inactive, writes nothing and warns once.
     *
     * The glue owns the bodies' `RigidBody::motionType`, following bodies' `Transform` and velocities, the joints'
     * motor modes and targets, the skeleton entity's `Transform` while it follows the root body, and the skeleton's
     * pose override entries: a write to any of them is overwritten next tick. `state` is the glue's too.
     */
    struct Ragdoll
    {
        /** @brief How every body moves, unless a body's own `mode` says otherwise. */
        RagdollMode mode = RagdollMode::Following;
        /** @brief How hard powered bodies' motors drive, in [0, 1]: read while `Powered` only. */
        float strength = 1.0f;
        /** @brief Whether the skeleton entity follows the root body horizontally and in yaw while any body is not
         * following. */
        bool followRoot = true;
        /** @brief The collision group the builder gave the bodies; informational: the bodies' own `CollisionGroup`s
         * are what physics reads. */
        std::uint32_t group = 0;
        /** @brief The root part's body entity: the hips' for a humanoid. */
        Entity rootBody;
        /** @brief What the glue found last tick. Glue-owned. */
        RagdollState state;
    };

    /** @brief What the ragdoll glue found for one body last tick. Glue-owned: a write to it is overwritten next tick.
     */
    struct RagdollBodyState
    {
        /** @brief The mode in effect: the body's own, or its ragdoll's while the body's is `Inherit`. */
        RagdollMode mode = RagdollMode::Following;
        /** @brief The bone the body's name resolved to, or `NoBone` (-1) while it is skipped. A literal, since the API
         * description records literal defaults only. */
        std::int32_t bone = -1;
    };

    /**
     * @brief A ragdoll body (ADR-0053): on each body entity (a `RigidBody` plus `Collider` entity), naming its ragdoll
     * and the bone it follows and drives, by name, with the body's pose relative to that bone.
     *
     * While following, the glue writes the body's `Transform` as the bone's simulation-pose world matrix × `offset`;
     * while simulating it writes the bone's world override target as the body × `offset`⁻¹. The offset is the
     * builder's, from the skeleton asset's rest pose, and the glue never recalibrates it: a rest-pose change in the
     * asset leaves it stale until the ragdoll is rebuilt. A body whose bone name matches nothing, whose entity has no
     * `RigidBody`, or whose `ragdoll` is not a live `Ragdoll` entity of its scene is skipped with one warning, and its
     * `state.bone` is `NoBone`.
     *
     * A raycast or contact naming a ragdoll body names its bone through `bone`: ragdoll bodies are the character's hit
     * boxes.
     */
    struct RagdollBody
    {
        /** @brief The skeleton entity carrying the `Ragdoll`: explicit, never "my parent". */
        Entity ragdoll;
        /** @brief The bone this body follows and drives, by engine name. */
        BoneName bone;
        /** @brief The body's pose relative to its bone. */
        Transform offset;
        /** @brief This body's own mode, or its ragdoll's. */
        RagdollBodyMode mode = RagdollBodyMode::Inherit;
        /** @brief Multiplies `Ragdoll::strength` for this body's joint to its parent while powered. */
        float strengthScale = 1.0f;
        /** @brief What the glue found last tick. Glue-owned. */
        RagdollBodyState state;
    };
} // namespace Fenrir
