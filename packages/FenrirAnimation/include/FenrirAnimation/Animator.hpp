#pragma once

#include <cstdint>

#include "FenrirAssets/AssetHandle.hpp"

namespace Fenrir
{
    /** @brief What a clip does at its boundary (ADR-0034). */
    enum class ClipLoop : std::uint8_t
    {
        /** @brief Wraps to the other end, however many laps one step covers. */
        Loop,
        /** @brief Clamps at the end, or at 0 in reverse, holds that pose and is finished. */
        Once,
    };

    /** @brief How a crossfade's progress becomes its blend weight (ADR-0036). */
    enum class FadeCurve : std::uint8_t
    {
        /** @brief The weight is the progress. */
        Linear,
        /** @brief Smoothstep, `t·t·(3 − 2t)`: the default. */
        Smooth,
    };

    /**
     * @brief The animator (ADR-0034): plain data on a skeleton entity naming the clip to play, how fast and in which
     * direction, its loop mode and whether it plays. Game code writes every field and FenrirAnimation never writes
     * any; it holds no time, because the playback cursor is FenrirAnimation's (read it with `Animation::Playback`,
     * move it with `Animation::Seek`).
     *
     * While `clip` names a clip, FenrirAnimation owns the skeleton's whole base pose: each frame it starts from the
     * rest pose and applies the clip's channels. With no `Animator`, or an empty `clip`, the base pose is left alone.
     * Changing `clip` restarts playback, at 0 or, with a negative `speed`, at the clip's duration, and crossfades from
     * the outgoing clip over `fadeSeconds` (ADR-0036): the outgoing clip keeps playing on its own cursor, a change
     * during a fade fades on from a snapshot of the blended pose, and 0 cuts. From no clip it fades from the retained
     * base pose; to no clip it cuts, leaving the pose as last sampled. It is the skeleton's base animation layer: a
     * full override, always first, under any `AnimationLayer`s.
     */
    struct Animator
    {
        /** @brief The clip to play; empty plays nothing and leaves the base pose alone. */
        AssetHandle<Clip> clip;
        /** @brief Seconds of clip per second of frame time: negative plays in reverse, 0 holds. */
        float speed = 1.0f;
        /** @brief Whether the clip wraps or clamps at its boundary. */
        ClipLoop loop = ClipLoop::Loop;
        /** @brief False freezes the playback cursor; the pose is still sampled there. */
        bool playing = true;
        /**
         * @brief How long a clip change crossfades, in seconds, by unscaled frame time, frozen while paused; 0 cuts. A
         * change during a fade applies to the next clip change.
         */
        float fadeSeconds = 0.0f;
        /** @brief The crossfade's curve: its weight as a function of progress. */
        FadeCurve fadeCurve = FadeCurve::Smooth;
    };
} // namespace Fenrir
