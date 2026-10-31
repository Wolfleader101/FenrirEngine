#pragma once

#include <cstdint>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirSkeleton/BoneAttachment.hpp"

namespace Fenrir
{
    /** @brief How a ragdoll's bodies move (ADR-0054). */
    enum class RagdollMode : std::uint8_t
    {
        /** @brief Every body kinematic, moved each tick to the simulation pose × its offset: hit boxes. */
        Following,
        /** @brief Every body dynamic, the glue writing world override targets into the skeleton each frame. */
        Simulating,
        /** @brief Dynamic, with each joint's motor driving towards the base pose at `Ragdoll::strength`. */
        Powered,
    };

    /** @brief A body's own mode: the ragdoll's, or one of the three, so a subtree can differ (a partial ragdoll). */
    enum class RagdollBodyMode : std::uint8_t
    {
        Inherit,
        Following,
        Simulating,
        Powered,
    };

    /** @brief What the glue read back about a ragdoll last tick. */
    struct RagdollState
    {
        /** @brief Whether any of its bodies is awake. */
        bool anyAwake = false;
        /** @brief How many bodies the glue found mapped to bones. */
        std::uint32_t bodies = 0;
        /** @brief Whether the ragdoll is inactive: no ready tick-posed SkeletonPose on its entity. */
        bool inactive = false;
    };

    /**
     * @brief A ragdoll, on the skeleton entity (the SkeletonPose entity, so a skeleton has at most one; ADR-0053).
     * Plain data: the glue turns `mode` into motion types and motor modes each tick (ADR-0054), and game code writes
     * nothing but these fields. Requires a tick-posed skeleton (`SkeletonPose::timing == PoseTiming::Tick`): on a
     * frame-posed one the ragdoll is inactive, with one warning.
     */
    struct Ragdoll
    {
        RagdollMode mode = RagdollMode::Following;
        /** @brief How hard the motors drive while powered, in [0, 1]: the glue turns it into each motor's maximum
         * torque (from the body's mass) and spring. */
        float strength = 0.5f;
        /** @brief While any body simulates, move the skeleton entity (position and yaw) so the hips land on the root
         * body. */
        bool followRoot = true;
        /** @brief The collision group its bodies share: the builder sets it to the skeleton entity's id. */
        std::uint32_t group = 0;
        /** @brief The body with no parent part: the hips. */
        Entity rootBody;
        /** @brief Read-back, overwritten by the glue each tick. */
        RagdollState state;
    };

    /** @brief What the glue read back about one body last tick. */
    struct RagdollBodyState
    {
        /** @brief The mode in effect: its own, or its ragdoll's while `Inherit`. */
        RagdollMode mode = RagdollMode::Following;
        /** @brief The bone the name resolved to, or `NoBone` (-1). */
        std::int32_t bone = -1;
    };

    /**
     * @brief A ragdoll body (ADR-0053): on each body entity (a RigidBody plus Collider), naming its ragdoll and the
     * bone it follows and drives, with the body's pose relative to that bone. Following writes the body's Transform as
     * `bone × offset`; simulating writes the bone's world target as `body × offset⁻¹`. The offset is the builder's,
     * from the rest pose, never recalibrated by the glue.
     */
    struct RagdollBody
    {
        /** @brief The skeleton entity carrying the Ragdoll. */
        Entity ragdoll;
        /** @brief The bone this body follows and drives, by engine name. */
        BoneName bone;
        /** @brief The body's pose relative to its bone. */
        Transform offset;
        /** @brief This body's own mode, or its ragdoll's. */
        RagdollBodyMode mode = RagdollBodyMode::Inherit;
        /** @brief SPIKE: the part's strengthScale, which the glue needs at run time: multiplies Ragdoll::strength for
         * this body's joint. The description decision kept it on the part only. */
        float strengthScale = 1.0f;
        /** @brief Read-back, overwritten by the glue each tick. */
        RagdollBodyState state;
    };
} // namespace Fenrir
