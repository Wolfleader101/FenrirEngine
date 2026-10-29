#pragma once

#include <cstdint>
#include <memory>

#include "FenrirAnimation/AnimationEvents.hpp"
#include "FenrirAnimation/Animator.hpp"
#include "FenrirAssets/SkinningSource.hpp"
#include "FenrirECS/Entity.hpp"

namespace Fenrir
{
    class App;
    class Scene;
    class Skeletons;

    namespace Detail
    {
        struct AnimationState;
    } // namespace Detail

    /** @brief How `Animation::Install` sets FenrirAnimation up. */
    struct AnimationConfig
    {
        /**
         * @brief The most animation events sent per skeleton instance per frame (ADR-0034). Crossings past it are
         * counted into one `AnimationEventsDropped`.
         */
        std::uint32_t maxEventsPerInstancePerFrame = 64;
    };

    /** @brief The order FenrirAnimation's `Animate` work runs at, so other base-pose writers can run before or after.
     */
    namespace AnimateOrder
    {
        /** @brief The sampler, which writes the whole base pose of each animated skeleton. */
        inline constexpr int Clips = 0;
    } // namespace AnimateOrder

    /** @brief Where an animator's playback stands, as `Animation::Playback` reads it. */
    struct PlaybackInfo
    {
        /**
         * @brief `Ready` while it plays (or holds) a clip on a `Ready` skeleton; `Unavailable` while the skeleton is
         * not `Ready` or the clip handle is stale; `Missing` for an entity with no `Animator`.
         */
        PoseState state = PoseState::Missing;
        /** @brief The playback cursor, in seconds: in [0, duration]. */
        double time = 0.0;
        /** @brief How many times playback wrapped, in either direction, since the clip started; saturating. */
        std::uint64_t completedLoops = 0;
        /** @brief Whether a `ClipLoop::Once` clip has reached its boundary and holds there. */
        bool finished = false;
        /** @brief Whether a crossfade is running. Always false for now: a clip change cuts. */
        bool fading = false;
        /** @brief The running crossfade's progress in [0, 1], before its curve; 0 when none runs. */
        float fadeProgress = 0.0f;
    };

    /**
     * @brief The owner and facade of FenrirAnimation (ADR-0028, ADR-0034): clip playback through the `Animator` on a
     * skeleton entity. Move-only. Destroy it before the `Skeletons` owner it was installed with, and that before the
     * App.
     *
     * **The sampler** is FenrirAnimation's work in the pose pipeline's `Animate` stage. Each frame, for every `Ready`
     * frame-posed skeleton whose `Animator` names a clip, it advances the playback cursor by `Time::deltaTime × speed`,
     * wraps or clamps it by the loop mode, writes the whole base pose from the rest pose and the clip's channels (each
     * of translation, rotation and scale applied on its own; a bone or channel the clip lacks keeps its rest value),
     * and sends the events the step crossed. Playback state lives in a private scene resource, reconciled against the
     * `Animator`s each frame; an `Animator` added mid-game starts at the next frame.
     *
     * **Bindings** map a clip's channels to bones once per (clip and revision, skeleton and revision), shared by every
     * instance of the pair and dropped when no instance uses it. A clip binds to a skeleton from its own model file by
     * node identity and to any other by exact engine name. A channel that binds to nothing is not played, and is
     * logged once per binding, naming the clip, the channel's target and path, and why: no bone matches, the match is
     * ambiguous (the skeleton repeats the bone's name), or the target is an ordinary node.
     *
     * **Unavailable.** A skeleton that is not `Ready` is neither sampled nor advanced. A clip handle that does not
     * resolve freezes the cursor and the pose, is logged once, and reads as `Unavailable`; once it resolves again the
     * cursor is clamped (`Once`) or wrapped (`Loop`) into the clip's duration, with no events for the gap. A clip whose
     * revision changes is rebound and its cursor clamped or wrapped the same way.
     *
     * **Root motion is not extracted**: a root bone's channel moves the bone relative to its skeleton entity.
     */
    class Animation
    {
      public:
        /**
         * @brief Turns FenrirAnimation on for `app`, adding its sampler to the `Animate` stage of `skeletons`. It
         * fails, with one logged diagnostic and an owner whose `IsValid()` is false that has added nothing, when
         * `skeletons` is not a valid owner (FenrirSkeleton is not installed), or when FenrirAnimation is already
         * installed in `app`.
         */
        [[nodiscard]] static Animation Install(App& app, Skeletons& skeletons, AnimationConfig config = {});

        Animation(Animation&& other) noexcept;
        Animation& operator=(Animation&& other) noexcept;
        Animation(const Animation&) = delete;
        Animation& operator=(const Animation&) = delete;
        /** @brief Stops the sampler and drops every scene's playback state. */
        ~Animation();

        /** @brief Whether this owner holds an install: false when Install failed, and once moved from. */
        [[nodiscard]] bool IsValid() const noexcept;

        /**
         * @brief Queues a seek of the entity's playback cursor to `seconds`, applied at its next evaluation, after any
         * clip change, so `clip = run; Seek(scene, e, 0.5)` starts `run` at 0.5 whichever was written first. The
         * latest pending seek wins. The cursor is clamped (`Once`) or wrapped (`Loop`) into the clip's duration. A
         * seek crosses no events, resets `finished`, works while paused (moving the pose, not resuming), and may come
         * before the entity has an `Animator` or a skeleton instance.
         */
        static void Seek(Scene& scene, Entity entity, double seconds);

        /** @brief Where the entity's playback stands. */
        [[nodiscard]] static PlaybackInfo Playback(Scene& scene, Entity entity);

      private:
        explicit Animation(std::shared_ptr<Detail::AnimationState> state) noexcept;

        void Shutdown() noexcept;

        // Shared with the sampler, which holds it weakly: once the owner is gone it does nothing.
        std::shared_ptr<Detail::AnimationState> m_state;
    };
} // namespace Fenrir
