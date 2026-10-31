#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include "FenrirAnimation/Animator.hpp"

namespace Fenrir
{
    struct ClipData;
    struct ClipEvent;

    namespace Detail
    {
        /** @brief No limit along a step: every crossing counts. */
        inline constexpr double Unlimited = std::numeric_limits<double>::infinity();

        /** @brief One lap's share of a step's crossings: events [begin, end) of the clip's list, in playback order. */
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

            /** @brief The event index of the span's `n`th crossing, in playback order. */
            [[nodiscard]] std::size_t At(std::size_t n) const noexcept
            {
                return reverse ? end - 1 - n : begin + n;
            }
        };

        /** @brief One crossed clip event: its index in the clip, its lap, and when in the step it was crossed. */
        struct Crossing
        {
            std::uint32_t index = 0;
            std::uint64_t loop = 0;
            /** @brief How far along the step, from 0 at its start to 1 at its end. */
            double fraction = 0.0;
        };

        /**
         * @brief What one step of a playback cursor crossed (ADR-0034), in playback order: the rest of the lap it
         * started in, any number of whole laps, then the part of the lap it ended in. Never enumerated wholesale: a
         * step across many laps is counted arithmetically and read only as far as a budget asks.
         */
        class Crossings
        {
          public:
            /** @brief How far the step went, in clip seconds, unclamped. */
            [[nodiscard]] double Length() const noexcept
            {
                return m_length;
            }

            /**
             * @brief How many crossings lie less than `limit` clip seconds along the step (all of them by default),
             * saturating; `events` are the stepped clip's.
             */
            [[nodiscard]] std::uint64_t CountBefore(const std::vector<ClipEvent>& events,
                                                    double limit = Unlimited) const;

            /** @brief Appends the earliest crossings before `limit`, at most `max`, in playback order. */
            void Earliest(const std::vector<ClipEvent>& events, double limit, std::uint64_t max,
                          std::vector<Crossing>& out) const;

          private:
            friend class PlaybackCursor;

            [[nodiscard]] double Distance(std::uint64_t lap, float time) const;
            [[nodiscard]] std::size_t CountBelow(const LapSpan& span, std::uint64_t lap,
                                                 const std::vector<ClipEvent>& events, double limit) const;

            LapSpan m_first;
            std::uint64_t m_wholeLaps = 0;
            std::optional<LapSpan> m_last;
            bool m_reverse = false;
            double m_from = 0.0;
            double m_duration = 0.0;
            double m_length = 0.0;
        };

        /** @brief What one step of a playback cursor did. */
        struct CursorStep
        {
            /** @brief What it crossed, or none when it did not move. */
            std::optional<Crossings> crossings;
            /** @brief Whether this step finished a `Once` clip. */
            bool finishedNow = false;
            /** @brief Whether the cursor wrapped or reversed, so a sampler's key cursors must search again. */
            bool jumped = false;
        };

        /**
         * @brief A playback cursor (GLOSSARY.md, ADR-0034): where one clip is, in seconds, and what moving it crosses.
         * It advances by a step's delta, wrapping or clamping by loop mode, counts completed laps and finishes a `Once`
         * clip once; a start, a refit to a changed clip and a seek place it without crossing anything. It knows the
         * clip only as `ClipData`, its duration and events; sampling and event delivery are its callers'.
         */
        class PlaybackCursor
        {
          public:
            /** @brief Starts playing a newly named clip: at 0, or at its duration when `speed` is negative. */
            void Start(const ClipData& clip, std::uint32_t revision, float speed);

            /** @brief Takes a changed clip: the time is placed into its duration, and a finished cursor holds its end.
             */
            void Refit(const ClipData& clip, std::uint32_t revision, ClipLoop loop);

            /** @brief Moves to `seconds`, clamped for `Once` and wrapped for `Loop`, crossing nothing and unfinishing.
             */
            void Seek(double seconds, ClipLoop loop);

            /**
             * @brief Advances by `delta` seconds (frame time × speed; 0 while paused): what it crossed, whether a
             * `Once` clip finished and whether it wrapped or reversed. A clip with no duration holds at 0 and crosses
             * its events once, on its first step while playing.
             */
            [[nodiscard]] CursorStep Step(const ClipData& clip, ClipLoop loop, bool playing, double delta);

            /** @brief Whether a clip has been started. */
            [[nodiscard]] bool Started() const noexcept
            {
                return m_started;
            }

            /** @brief The clip revision it was started or last refitted at. */
            [[nodiscard]] std::uint32_t Revision() const noexcept
            {
                return m_revision;
            }

            /** @brief Where the clip is, in seconds. */
            [[nodiscard]] double Time() const noexcept
            {
                return m_time;
            }

            /** @brief How many laps it has completed, saturating; a reverse lap counts too. */
            [[nodiscard]] std::uint64_t CompletedLoops() const noexcept
            {
                return m_completedLoops;
            }

            /** @brief Whether a `Once` clip has reached the boundary it plays towards and holds there. */
            [[nodiscard]] bool Finished() const noexcept
            {
                return m_finished;
            }

          private:
            bool m_started = false;
            std::uint32_t m_revision = 0;
            double m_duration = 0.0;
            double m_time = 0.0;
            std::uint64_t m_completedLoops = 0;
            bool m_finished = false;
            // Which boundary a finished clip holds at: its end (forward) or 0 (reverse).
            bool m_finishedAtEnd = true;
            // Whether playback has not moved since it started, so its first move crosses the starting boundary too.
            bool m_atStart = true;
            // The sign of the last move, so a reversal is reported as a jump.
            int m_direction = 0;
        };
    } // namespace Detail
} // namespace Fenrir
