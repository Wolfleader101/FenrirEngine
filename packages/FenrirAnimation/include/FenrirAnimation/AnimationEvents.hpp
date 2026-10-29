#pragma once

#include <cstdint>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirAssets/ClipData.hpp"
#include "FenrirECS/Entity.hpp"

namespace Fenrir
{
    /**
     * @brief An animation event (ADR-0034): sent from the `Animate` stage when a playback cursor crosses one of its
     * clip's timed events. Read it with an `EventReader<AnimationEvent>`: a `PostUpdate` reader that runs after the
     * pose pipeline sees it the same frame, an `Update` reader the next frame.
     *
     * Forward playback crosses the events in (previous, current], reverse playback those in [current, previous), in
     * playback order, every lap of a wrap included; when playback starts, the event at its starting boundary is
     * crossed too. Seeks, clip changes, pauses, a speed of 0 and recovery from an unavailable clip cross nothing.
     * Delivery is best-effort: at most `AnimationConfig::maxEventsPerInstancePerFrame` are sent per skeleton per
     * frame, the earliest, and the rest are counted in one `AnimationEventsDropped`.
     */
    struct AnimationEvent
    {
        /** @brief The skeleton entity whose playback crossed the event. */
        Entity skeleton;
        /** @brief The layer that played the clip: the skeleton entity for its `Animator`. */
        Entity layer;
        /** @brief The clip. */
        AssetHandle<Clip> clip;
        /** @brief The clip's revision when the event was crossed: `index` names an event only in this revision. */
        std::uint32_t clipRevision = 0;
        /** @brief The event's index in the clip's event list. */
        std::uint32_t index = 0;
        /** @brief The id of the event's name, for comparing names cheaply. */
        ClipEventId nameId;
        /** @brief The event's authored time, in seconds. */
        float time = 0.0f;
        /** @brief The lap the crossing happened in: the playback's completed loops at that moment. */
        std::uint64_t loop = 0;
    };

    /**
     * @brief Sent once, from the `Animate` stage, when a `ClipLoop::Once` clip reaches its boundary: each time its
     * playback becomes finished (ADR-0034).
     */
    struct ClipFinished
    {
        /** @brief The skeleton entity. */
        Entity skeleton;
        /** @brief The layer that played the clip: the skeleton entity for its `Animator`. */
        Entity layer;
        /** @brief The clip that finished. */
        AssetHandle<Clip> clip;
    };

    /**
     * @brief Sent from the `Animate` stage when one frame's playback crossed more clip events than the budget lets
     * through (ADR-0034): how many were crossed and not sent. The drop is also logged, once per skeleton instance.
     */
    struct AnimationEventsDropped
    {
        /** @brief The skeleton entity. */
        Entity skeleton;
        /** @brief The layer that played the clip: the skeleton entity for its `Animator`. */
        Entity layer;
        /** @brief The clip whose events were dropped. */
        AssetHandle<Clip> clip;
        /** @brief How many crossings were not sent, saturating at the largest `uint64_t`. */
        std::uint64_t dropped = 0;
    };
} // namespace Fenrir
