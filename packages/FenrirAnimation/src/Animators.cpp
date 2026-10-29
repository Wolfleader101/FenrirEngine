#include "Animators.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <span>
#include <string>
#include <utility>

#include "AnimationState.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirSkeleton/Skeletons.hpp"

namespace Fenrir::Detail
{
    namespace
    {
        constexpr std::uint64_t MaxCount = std::numeric_limits<std::uint64_t>::max();

        std::uint64_t SaturatingAdd(std::uint64_t a, std::uint64_t b)
        {
            return a > MaxCount - b ? MaxCount : a + b;
        }

        std::uint64_t SaturatingMultiply(std::uint64_t a, std::uint64_t b)
        {
            return a != 0 && b > MaxCount / a ? MaxCount : a * b;
        }

        // A whole number of laps as a count, saturating: a large enough step covers more laps than a u64 holds.
        std::uint64_t LapCount(double laps)
        {
            return laps >= 18446744073709551615.0 ? MaxCount : static_cast<std::uint64_t>(laps);
        }

        std::string Describe(Entity entity)
        {
            if (entity.IsValid() && entity.HasComponent<Name>() && entity.GetComponent<Name>().Get()[0] != '\0')
            {
                return std::format("'{}'", entity.GetComponent<Name>().Get());
            }
            return std::format("entity {}", entity.GetId());
        }

        // A time placed into the clip: clamped for Once, wrapped for Loop, 0 for a clip with no duration.
        double Fit(double time, double duration, ClipLoop loop)
        {
            if (duration <= 0.0)
            {
                return 0.0;
            }
            if (loop == ClipLoop::Once)
            {
                return std::clamp(time, 0.0, duration);
            }
            const double wrapped = time - std::floor(time / duration) * duration;
            return wrapped >= duration ? 0.0 : wrapped;
        }

        // One lap's share of a step's crossings: events [begin, end) of the clip's list, in playback order.
        struct LapSpan
        {
            std::size_t begin = 0;
            std::size_t end = 0;
            bool reverse = false;
            std::uint64_t loop = 0;

            [[nodiscard]] std::size_t Size() const noexcept
            {
                return end - begin;
            }
        };

        // The events a lap crosses between `from` and `to` (from before to), each end included or not.
        LapSpan Span(const std::vector<ClipEvent>& events, double from, bool includeFrom, double to, bool includeTo,
                     bool reverse, std::uint64_t loop)
        {
            const auto timeOf = [](const ClipEvent& event) { return static_cast<double>(event.time); };
            const auto first = includeFrom ? std::ranges::lower_bound(events, from, {}, timeOf)
                                           : std::ranges::upper_bound(events, from, {}, timeOf);
            const auto last = includeTo ? std::ranges::upper_bound(events, to, {}, timeOf)
                                        : std::ranges::lower_bound(events, to, {}, timeOf);
            const auto begin = static_cast<std::size_t>(first - events.begin());
            const auto end = std::max(begin, static_cast<std::size_t>(last - events.begin()));
            return {begin, end, reverse, loop};
        }

        // A step's crossings, in playback order: the rest of the lap it started in, any number of whole laps, then
        // the part of the lap it ended in.
        struct Crossings
        {
            LapSpan first;
            std::uint64_t wholeLaps = 0;
            std::optional<LapSpan> last;
            bool reverse = false;
        };

        // Sends the step's crossings up to the budget, the earliest first, and counts the rest arithmetically.
        class EventSender
        {
          public:
            EventSender(App& app, std::uint32_t budget, Entity skeleton, AssetHandle<Clip> clip, std::uint32_t revision,
                        const ClipData& data) noexcept
                : m_app(app), m_budget(budget), m_skeleton(skeleton), m_clip(clip), m_revision(revision), m_data(data)
            {
            }

            void Send(const Crossings& crossings)
            {
                const std::uint64_t perLap = m_data.events.size();
                Lap(crossings.first);
                for (std::uint64_t lap = 0; lap < crossings.wholeLaps && m_sent < m_budget; ++lap)
                {
                    LapSpan whole{0, m_data.events.size(), crossings.reverse,
                                  SaturatingAdd(crossings.first.loop, lap + 1)};
                    Lap(whole);
                }
                if (crossings.last)
                {
                    Lap(*crossings.last);
                }
                m_total = SaturatingAdd(crossings.first.Size(), SaturatingMultiply(crossings.wholeLaps, perLap));
                m_total = SaturatingAdd(m_total, crossings.last ? crossings.last->Size() : 0);
            }

            [[nodiscard]] std::uint64_t Dropped() const noexcept
            {
                return m_total - m_sent;
            }

          private:
            void Lap(const LapSpan& span)
            {
                for (std::size_t n = 0; n < span.Size() && m_sent < m_budget; ++n)
                {
                    const std::size_t index = span.reverse ? span.end - 1 - n : span.begin + n;
                    const ClipEvent& event = m_data.events[index];
                    m_app.SendEvent(AnimationEvent{m_skeleton, m_skeleton, m_clip, m_revision,
                                                   static_cast<std::uint32_t>(index), ClipEventId::Of(event.name),
                                                   event.time, span.loop});
                    ++m_sent;
                }
            }

            App& m_app;
            std::uint64_t m_budget;
            Entity m_skeleton;
            AssetHandle<Clip> m_clip;
            std::uint32_t m_revision;
            const ClipData& m_data;
            std::uint64_t m_sent = 0;
            std::uint64_t m_total = 0;
        };

        void SearchKeys(AnimatorPlayback& playback)
        {
            for (KeyCursor& key : playback.keys)
            {
                key.search = true;
            }
        }

        // Restarts playback of a newly named clip: at 0, or at its duration in reverse (ADR-0034).
        void Start(AnimatorPlayback& playback, const ClipData& data, std::uint32_t revision, float speed)
        {
            playback.started = true;
            playback.clipRevision = revision;
            playback.duration = data.duration;
            playback.cursor = speed < 0.0f ? playback.duration : 0.0;
            playback.completedLoops = 0;
            playback.finished = false;
            playback.atStart = true;
            playback.direction = 0;
            playback.keys.assign(data.channels.size(), KeyCursor{});
        }

        // Moves the cursor by `delta` and works out what the move crossed; finishing is the caller's.
        Crossings Move(AnimatorPlayback& playback, const ClipData& data, ClipLoop loop, double delta, bool& reached)
        {
            const double duration = playback.duration;
            const double from = playback.cursor;
            const double to = from + delta;
            const bool forward = delta > 0.0;
            const bool start = playback.atStart;
            const std::uint64_t loops = playback.completedLoops;
            Crossings crossings;
            crossings.reverse = !forward;
            reached = false;
            if (loop == ClipLoop::Once)
            {
                const double clamped = std::clamp(to, 0.0, duration);
                reached = forward ? clamped >= duration : clamped <= 0.0;
                crossings.first = forward ? Span(data.events, from, start, clamped, true, false, loops)
                                          : Span(data.events, clamped, true, from, start, true, loops);
                playback.cursor = clamped;
                return crossings;
            }
            const double lapsCrossed = std::floor(to / duration);
            double cursor = to - lapsCrossed * duration;
            std::uint64_t laps = LapCount(std::abs(lapsCrossed));
            if (cursor >= duration)
            {
                // Rounding can land a wrap on the boundary itself: that is the next lap's start.
                cursor -= duration;
                laps = forward ? SaturatingAdd(laps, 1) : laps - 1;
            }
            if (laps == 0)
            {
                crossings.first = forward ? Span(data.events, from, start, cursor, true, false, loops)
                                          : Span(data.events, cursor, true, from, start, true, loops);
            }
            else if (forward)
            {
                crossings.first = Span(data.events, from, start, duration, true, false, loops);
                crossings.wholeLaps = laps - 1;
                crossings.last = Span(data.events, 0.0, true, cursor, true, false, SaturatingAdd(loops, laps));
            }
            else
            {
                crossings.first = Span(data.events, 0.0, true, from, start, true, loops);
                crossings.wholeLaps = laps - 1;
                crossings.last = Span(data.events, cursor, true, duration, true, true, SaturatingAdd(loops, laps));
            }
            playback.cursor = cursor;
            playback.completedLoops = SaturatingAdd(loops, laps);
            if (laps != 0)
            {
                SearchKeys(playback);
            }
            return crossings;
        }
    } // namespace

    void Animators::Seek(Entity entity, double seconds)
    {
        m_seeks.insert_or_assign(entity.GetId(), std::pair{entity, seconds});
    }

    const AnimatorPlayback* Animators::Find(Entity entity) const
    {
        const auto found = m_playbacks.find(entity.GetId());
        return found == m_playbacks.end() || found->second.entity != entity ? nullptr : &found->second;
    }

    void Animators::Run(AnimationState& state, AnimateContext& context)
    {
        const AssetStore* store = state.app->TryGetResource<AssetStore>();
        if (store == nullptr)
        {
            return;
        }
        // Reconcile: state for an entity whose Animator is gone, or that is gone itself, is released.
        std::erase_if(m_playbacks, [](const auto& entry) {
            const Entity& entity = entry.second.entity;
            return !entity.IsValid() || !entity.HasComponent<Animator>();
        });
        std::erase_if(m_seeks, [](const auto& entry) { return !entry.second.first.IsValid(); });

        for (const Entity& entity : context.Instances())
        {
            if (entity.HasComponent<Animator>())
            {
                Evaluate(state, *store, context.GetScene(), entity, context.Step());
            }
        }
        m_bindings.SweepUnused();
    }

    void Animators::Evaluate(AnimationState& state, const AssetStore& store, Scene& scene, Entity entity, double step)
    {
        App& app = *state.app;
        ILogger& logger = *app.Logger();
        const Animator animator = entity.GetComponent<Animator>();
        auto [found, made] = m_playbacks.try_emplace(entity.GetId());
        AnimatorPlayback& playback = found->second;
        if (made || playback.entity != entity)
        {
            playback = AnimatorPlayback{};
            playback.entity = entity;
        }
        if (animator.clip != playback.clip)
        {
            // A clip change restarts once the new clip's data is seen; for now it cuts.
            const bool reportedDrop = playback.reportedDrop;
            playback = AnimatorPlayback{};
            playback.entity = entity;
            playback.clip = animator.clip;
            playback.reportedDrop = reportedDrop;
        }
        if (animator.clip.IsEmpty())
        {
            // No active animation: the base pose is left alone, and any pending seek waits for a clip.
            return;
        }
        const ClipData* clip = store.TryGet(animator.clip);
        if (clip == nullptr)
        {
            playback.unavailable = true;
            if (!playback.reportedUnavailable)
            {
                playback.reportedUnavailable = true;
                logger.Warn("{}", std::format("FenrirAnimation: the clip {}.{} that {}'s Animator names does not "
                                              "resolve; its playback is frozen until it does",
                                              animator.clip.index, animator.clip.generation, Describe(entity)));
            }
            return;
        }
        playback.unavailable = false;
        playback.reportedUnavailable = false;

        const std::uint32_t revision = store.Revision(animator.clip);
        if (!playback.started)
        {
            Start(playback, *clip, revision, animator.speed);
        }
        else if (revision != playback.clipRevision)
        {
            // A changed clip: the cursor is placed into its duration, crossing nothing (ADR-0034).
            playback.clipRevision = revision;
            playback.duration = clip->duration;
            playback.cursor = playback.finished ? (playback.finishedAtEnd ? playback.duration : 0.0)
                                                : Fit(playback.cursor, playback.duration, animator.loop);
            playback.keys.assign(clip->channels.size(), KeyCursor{});
        }
        if (const auto seek = m_seeks.find(entity.GetId()); seek != m_seeks.end())
        {
            if (std::isfinite(seek->second.second))
            {
                playback.cursor = Fit(seek->second.second, playback.duration, animator.loop);
                playback.finished = false;
                playback.atStart = false;
                SearchKeys(playback);
            }
            else
            {
                logger.Warn("{}", std::format("FenrirAnimation: a seek of {} to a non-finite time was ignored",
                                              Describe(entity)));
            }
            m_seeks.erase(seek);
        }

        const auto deliver = [&](const Crossings& crossings) {
            EventSender sender(app, state.config.maxEventsPerInstancePerFrame, entity, animator.clip, revision, *clip);
            sender.Send(crossings);
            const std::uint64_t dropped = sender.Dropped();
            if (dropped == 0)
            {
                return;
            }
            app.SendEvent(AnimationEventsDropped{entity, entity, animator.clip, dropped});
            if (!playback.reportedDrop)
            {
                playback.reportedDrop = true;
                logger.Warn("{}", std::format("FenrirAnimation: {} crossed more clip events in one frame than its "
                                              "budget of {}; {} were dropped (logged once per skeleton)",
                                              Describe(entity), state.config.maxEventsPerInstancePerFrame, dropped));
            }
        };
        const double delta = animator.playing ? step * static_cast<double>(animator.speed) : 0.0;
        if (animator.loop == ClipLoop::Loop)
        {
            playback.finished = false;
        }
        if (playback.duration <= 0.0)
        {
            // A clip with no duration holds its first keys: it never wraps, and starting crosses its events once.
            playback.cursor = 0.0;
            if (animator.playing && playback.atStart)
            {
                playback.atStart = false;
                deliver({Span(clip->events, 0.0, true, 0.0, true, false, 0), 0, std::nullopt, false});
                if (animator.loop == ClipLoop::Once && !playback.finished)
                {
                    playback.finished = true;
                    app.SendEvent(ClipFinished{entity, entity, animator.clip});
                }
            }
        }
        else if (delta != 0.0)
        {
            const int direction = delta > 0.0 ? 1 : -1;
            if (playback.finished && (direction > 0) != playback.finishedAtEnd)
            {
                // Playing away from the boundary it finished at.
                playback.finished = false;
            }
            if (playback.direction != 0 && playback.direction != direction)
            {
                SearchKeys(playback);
            }
            playback.direction = direction;
            bool reached = false;
            const Crossings crossings = Move(playback, *clip, animator.loop, delta, reached);
            playback.atStart = false;
            deliver(crossings);
            if (reached && !playback.finished)
            {
                playback.finished = true;
                playback.finishedAtEnd = direction > 0;
                app.SendEvent(ClipFinished{entity, entity, animator.clip});
            }
        }

        // The whole base pose, from rest: each bound channel applied on its own (ADR-0034).
        const AssetHandle<Skeleton> skeletonHandle = entity.GetComponent<SkeletonPose>().skeleton;
        const SkeletonData* skeleton = store.TryGet(skeletonHandle);
        std::span<Transform> base = Skeletons::EditBasePose(scene, entity);
        if (skeleton == nullptr || base.size() != skeleton->BoneCount())
        {
            return;
        }
        const BindingKey key{animator.clip, revision, skeletonHandle, store.Revision(skeletonHandle)};
        const Binding& binding = m_bindings.Get(store, logger, key, *clip, *skeleton);
        for (std::size_t i = 0; i < base.size(); ++i)
        {
            base[i] = skeleton->bones[i].rest;
        }
        const auto time = static_cast<float>(playback.cursor);
        for (std::size_t c = 0; c < clip->channels.size(); ++c)
        {
            const std::int32_t bone = binding.bones[c];
            if (bone != NoBone)
            {
                const ClipChannel& channel = clip->channels[c];
                ApplySample(channel.path, SampleChannel(channel, time, playback.keys[c]),
                            base[static_cast<std::size_t>(bone)]);
            }
        }
    }
} // namespace Fenrir::Detail
