#pragma once

#include <cstdint>

#include "FenrirAssets/AssetHandle.hpp"

namespace Fenrir
{
    /** @brief When a skeleton's base pose is evaluated (ADR-0040). */
    enum class PoseTiming : std::uint8_t
    {
        /** @brief Once a frame, in the frame pass of the pose pipeline: the default. */
        Frame,
        /**
         * @brief Once per fixed tick, at tick time: a tick-posed skeleton, for bodies that follow its bones. Its base
         * pose is evaluated by the tick pass of `Animate` in `PreTick`, giving the simulation pose, and the frame draws
         * the last two ticks' blend at `Time::accumulator / Time::tickRate`, with modifiers on top: the render pose.
         */
        Tick,
    };

    /** @brief Which of a skeleton's poses a base-pose read gives (ADR-0040). */
    enum class PoseKind : std::uint8_t
    {
        /** @brief The pose this frame draws, the render pose: the default. */
        Render,
        /**
         * @brief The pose the last fixed tick evaluated, the simulation pose: the base pose at tick time, with no
         * modifiers, so a world read of it is the bone's model matrix in that pose. On a frame-posed skeleton it is the
         * render pose.
         */
        Simulation,
    };

    /**
     * @brief The skeleton pose (ADR-0031): plain data on a skeleton entity, naming the skeleton asset its pose
     * instances. One per entity. FenrirSkeleton keeps the pose itself (base and final locals, model matrices, the
     * palette) in a private scene resource, made for the component when the pose pipeline next reconciles, or at once
     * by `Skeletons::Ensure`. Changing `skeleton` rebuilds the instance at the rest pose; switching `timing` is a field
     * edit, taking effect at the next pass's reconcile. Switching to `Tick` seeds both tick poses with the current base
     * pose, so nothing jumps; switching back to `Frame` resumes frame `Animate` from where the tick clock stopped.
     */
    struct SkeletonPose
    {
        /** @brief The skeleton asset this instance poses. Empty or stale, the instance is `Unavailable`. */
        AssetHandle<Skeleton> skeleton;
        /** @brief When its base pose is evaluated. */
        PoseTiming timing = PoseTiming::Frame;
    };
} // namespace Fenrir
