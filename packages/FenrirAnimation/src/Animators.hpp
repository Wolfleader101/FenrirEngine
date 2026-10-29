#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "Bindings.hpp"
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

        /** @brief One animator's playback state: everything ADR-0034 keeps out of the `Animator` component. */
        struct AnimatorPlayback
        {
            Entity entity;
            // The clip this state plays, as the Animator last named it; started once its data was first seen.
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
            // Whether the clip handle failed to resolve at the last evaluation, and whether that has been logged.
            bool unavailable = false;
            bool reportedUnavailable = false;
            // Whether a drop past the event budget has been logged for this instance.
            bool reportedDrop = false;
            std::vector<KeyCursor> keys;
        };

        /**
         * @brief FenrirAnimation's private scene resource (ADR-0034): each animator's playback state, the pending
         * seeks and the binding cache. Reconciled against the `Animator`s at every sampling pass, as `SkeletonPoses` is
         * against the `SkeletonPose`s.
         */
        class Animators
        {
          public:
            /** @brief Queues a seek, replacing any pending one for the entity. */
            void Seek(Entity entity, double seconds);

            /** @brief The entity's playback state, or null before its first evaluation. */
            [[nodiscard]] const AnimatorPlayback* Find(Entity entity) const;

            /** @brief One `Animate` pass: reconcile, then advance, sample and send events for each listed instance. */
            void Run(AnimationState& state, AnimateContext& context);

          private:
            void Evaluate(AnimationState& state, const AssetStore& store, Scene& scene, Entity entity, double step);

            std::unordered_map<std::uint32_t, AnimatorPlayback> m_playbacks;
            std::unordered_map<std::uint32_t, std::pair<Entity, double>> m_seeks;
            Bindings m_bindings;
        };
    } // namespace Detail
} // namespace Fenrir
