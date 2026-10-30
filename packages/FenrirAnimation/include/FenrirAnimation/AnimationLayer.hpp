#pragma once

#include <cstdint>

#include "FenrirAnimation/Animator.hpp"
#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirECS/Entity.hpp"

namespace Fenrir
{
    /** @brief How an animation layer combines with the pose beneath it (ADR-0036). */
    enum class LayerMode : std::uint8_t
    {
        /** @brief Blends toward the layer's clip: translation and scale lerp, rotation nlerps along the shortest arc.
         * Channels the clip does not animate pass through. */
        Override,
        /** @brief Adds the clip's difference from its reference pose onto the pose beneath. Channels the clip does not
         * animate add nothing. */
        Additive,
    };

    /** @brief What an additive layer's clip is measured from (ADR-0036). */
    enum class AdditiveReference : std::uint8_t
    {
        /** @brief The clip's own pose at time 0, rest where the clip is silent: the default. */
        ClipStart,
        /** @brief The skeleton's rest pose. */
        RestPose,
    };

    /**
     * @brief An animation layer (ADR-0036): plain data on any entity, playing a clip over the skeleton entity
     * `skeleton` after its base `Animator`. Game code writes every field and FenrirAnimation never writes any; the
     * layer's cursors, fade and caches are FenrirAnimation's (read them with `Animation::Playback(scene, layer)`, move
     * the cursor with `Animation::Seek(scene, layer, seconds)`).
     *
     * A skeleton's layers apply in sequence, by `order` then entity, each at an effective weight of `weight × mask
     * weight(bone)` clamped to [0, 1], with nothing normalised. A layer with no base `Animator` layers over the
     * skeleton's retained base pose. The clock and fade fields work as the `Animator`'s do; a weight of 0 removes the
     * layer's effect and silences its events but never stops its clock.
     */
    struct AnimationLayer
    {
        /** @brief The skeleton entity it layers onto; it need not be the layer's ECS parent. */
        Entity skeleton;
        /** @brief Where it applies among the skeleton's layers: after the base `Animator`, by order, then by entity. */
        int order = 0;
        /** @brief How much of it shows, clamped to [0, 1]. Never touches its clock. */
        float weight = 1.0f;
        /** @brief Override or Additive. */
        LayerMode mode = LayerMode::Override;
        /** @brief The bones it reaches, by engine name; empty reaches every bone at 1. */
        AssetHandle<BoneMask> mask;
        /** @brief What an additive clip is measured from; ignored by override layers. */
        AdditiveReference reference = AdditiveReference::ClipStart;
        /** @brief The clip to play; empty contributes nothing. */
        AssetHandle<Clip> clip;
        /** @brief Seconds of clip per second of frame time: negative plays in reverse, 0 holds. */
        float speed = 1.0f;
        /** @brief Whether the clip wraps or clamps at its boundary. */
        ClipLoop loop = ClipLoop::Loop;
        /** @brief False freezes the cursor and any running fade; the pose is still sampled there. */
        bool playing = true;
        /** @brief How long a clip change crossfades, in seconds; 0 cuts. */
        float fadeSeconds = 0.0f;
        /** @brief The crossfade's curve. */
        FadeCurve fadeCurve = FadeCurve::Smooth;
    };
} // namespace Fenrir
