#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "Bindings.hpp"
#include "Blending.hpp"
#include "FenrirAnimation/Animation.hpp"
#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirECS/Entity.hpp"
#include "Sampling.hpp"

namespace Fenrir
{
    class AnimateContext;
    class AssetStore;

    namespace Detail
    {
        struct AnimationState;

        /** @brief One clip's playback: its cursor and what ADR-0034 keeps beside it. A layer has one, or two in a fade.
         */
        struct ClipTrack
        {
            // The clip this track plays; started once its data was first seen.
            AssetHandle<Clip> clip;
            bool started = false;
            std::uint32_t clipRevision = 0;
            double duration = 0.0;
            // The playback cursor, in seconds.
            double cursor = 0.0;
            std::uint64_t completedLoops = 0;
            bool finished = false;
            // Which boundary a finished clip holds at: its end (forward) or 0 (reverse).
            bool finishedAtEnd = true;
            // Whether playback has not moved since it started, so its first move crosses the starting boundary too.
            bool atStart = true;
            // The sign of the last move, so a reversal sends the key cursors searching.
            int direction = 0;
            std::vector<KeyCursor> keys;
        };

        /**
         * @brief One layer's playback state (ADR-0036): the base `Animator`'s, keyed by its skeleton entity, or an
         * `AnimationLayer`'s, keyed by the layer entity. Everything the components leave out.
         */
        struct LayerPlayback
        {
            Entity entity;
            // The skeleton entity the state was built on: a layer retargeted to another restarts.
            Entity skeleton;
            // The clip as the component last named it, and its track.
            ClipTrack current;
            // During a fade: the outgoing clip, on its own cursor, or a snapshot of the interrupted blend.
            std::optional<ClipTrack> outgoing;
            std::optional<LayerPose> snapshot;
            bool fading = false;
            double fadeProgress = 0.0;
            double fadeSeconds = 0.0;
            FadeCurve fadeCurve = FadeCurve::Smooth;
            // Whether the current clip failed to resolve at the last evaluation, and whether that has been logged.
            bool unavailable = false;
            bool reportedUnavailable = false;
            bool reportedMask = false;
        };

        /** @brief What FenrirAnimation keeps per skeleton instance across its layers. */
        struct SkeletonBlend
        {
            Entity entity;
            // The pose under the layers (the base layer's result, or the retained base pose), and what was written.
            std::vector<Transform> foundation;
            std::vector<Transform> written;
            // Whether a drop past the event budget has been logged for this instance.
            bool reportedDrop = false;
        };

        /**
         * @brief FenrirAnimation's private scene resource (ADR-0034, ADR-0036): each layer's playback state, each
         * skeleton's blend state, the pending seeks and the binding, mask and reference caches. Reconciled against the
         * `Animator`s and `AnimationLayer`s at every sampling pass, as `SkeletonPoses` is against the `SkeletonPose`s.
         */
        class Animators
        {
          public:
            /** @brief Queues a seek, replacing any pending one for the entity. */
            void Seek(Entity entity, double seconds);

            /** @brief The base layer's playback state on a skeleton entity, or null before its first evaluation. */
            [[nodiscard]] const LayerPlayback* FindBase(Entity entity) const;

            /** @brief A layer entity's playback state, or null before its first evaluation. */
            [[nodiscard]] const LayerPlayback* FindLayer(Entity entity) const;

            /** @brief One `Animate` pass: reconcile, then evaluate every listed skeleton with its layers. */
            void Run(AnimationState& state, AnimateContext& context);

          private:
            struct Pass;

            void Evaluate(Pass& pass, Entity skeleton, const std::vector<Entity>& layers);

            std::unordered_map<std::uint32_t, LayerPlayback> m_bases;
            std::unordered_map<std::uint32_t, LayerPlayback> m_layers;
            std::unordered_map<std::uint32_t, SkeletonBlend> m_skeletons;
            std::unordered_map<std::uint32_t, std::pair<Entity, double>> m_seeks;
            Bindings m_bindings;
            MaskBindings m_masks;
            ReferencePoses m_references;
        };
    } // namespace Detail
} // namespace Fenrir
