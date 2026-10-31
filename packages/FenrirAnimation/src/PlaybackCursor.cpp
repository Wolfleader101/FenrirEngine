#include "PlaybackCursor.hpp"

#include <algorithm>
#include <cmath>

#include "FenrirAssets/ClipData.hpp"

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
    } // namespace

    // How far along the step a crossing of `time` lies: in the first span (lap 0), whole lap `lap` (1-based) or the
    // last span (lap wholeLaps + 1).
    double Crossings::Distance(std::uint64_t lap, float time) const
    {
        const double t = time;
        if (lap == 0)
        {
            return m_reverse ? m_from - t : t - m_from;
        }
        const double toEnd = m_reverse ? m_from : m_duration - m_from;
        const double within = m_reverse ? m_duration - t : t;
        return toEnd + static_cast<double>(lap - 1) * m_duration + within;
    }

    // How many of a span's crossings lie before `limit` along the step: they come in playback order, so their
    // distances only grow.
    std::size_t Crossings::CountBelow(const LapSpan& span, std::uint64_t lap, const std::vector<ClipEvent>& events,
                                      double limit) const
    {
        std::size_t low = 0;
        std::size_t high = span.Size();
        while (low < high)
        {
            const std::size_t mid = (low + high) / 2;
            if (Distance(lap, events[span.At(mid)].time) < limit)
            {
                low = mid + 1;
            }
            else
            {
                high = mid;
            }
        }
        return low;
    }

    // Counted arithmetically: whole laps times events per lap, plus the partial laps, never enumerated (ADR-0034,
    // ADR-0036).
    std::uint64_t Crossings::CountBefore(const std::vector<ClipEvent>& events, double limit) const
    {
        const std::uint64_t perLap = events.size();
        if (limit == Unlimited)
        {
            const std::uint64_t total = SaturatingAdd(m_first.Size(), SaturatingMultiply(m_wholeLaps, perLap));
            return SaturatingAdd(total, m_last ? m_last->Size() : 0);
        }
        std::uint64_t total = CountBelow(m_first, 0, events, limit);
        if (total < m_first.Size() || perLap == 0)
        {
            return total;
        }
        if (m_wholeLaps > 0)
        {
            // Whole lap k (from 1) lies wholly before the limit when its last crossing does.
            const double toEnd = m_reverse ? m_from : m_duration - m_from;
            const double lastWithin = m_reverse ? m_duration - events.front().time : events.back().time;
            const double room = (limit - toEnd - lastWithin) / m_duration;
            const std::uint64_t fullLaps = room <= 0.0 ? 0 : std::min(m_wholeLaps, LapCount(std::ceil(room)));
            total = SaturatingAdd(total, SaturatingMultiply(fullLaps, perLap));
            if (fullLaps < m_wholeLaps)
            {
                const LapSpan whole{0, events.size(), m_reverse, 0};
                return SaturatingAdd(total, CountBelow(whole, fullLaps + 1, events, limit));
            }
        }
        if (m_last)
        {
            total = SaturatingAdd(total, CountBelow(*m_last, m_wholeLaps + 1, events, limit));
        }
        return total;
    }

    void Crossings::Earliest(const std::vector<ClipEvent>& events, double limit, std::uint64_t max,
                             std::vector<Crossing>& out) const
    {
        std::uint64_t taken = 0;
        const auto take = [&](const LapSpan& span, std::uint64_t lap) {
            for (std::size_t n = 0; n < span.Size() && taken < max; ++n)
            {
                const std::size_t index = span.At(n);
                const double distance = Distance(lap, events[index].time);
                if (distance >= limit)
                {
                    return false;
                }
                out.push_back(
                    {static_cast<std::uint32_t>(index), span.loop, m_length > 0.0 ? distance / m_length : 0.0});
                ++taken;
            }
            return taken < max;
        };
        if (!take(m_first, 0) || events.empty())
        {
            return;
        }
        for (std::uint64_t lap = 0; lap < m_wholeLaps; ++lap)
        {
            if (!take({0, events.size(), m_reverse, SaturatingAdd(m_first.loop, lap + 1)}, lap + 1))
            {
                return;
            }
        }
        if (m_last)
        {
            take(*m_last, m_wholeLaps + 1);
        }
    }

    void PlaybackCursor::Start(const ClipData& clip, std::uint32_t revision, float speed)
    {
        m_started = true;
        m_revision = revision;
        m_duration = clip.duration;
        m_time = speed < 0.0f ? m_duration : 0.0;
        m_completedLoops = 0;
        m_finished = false;
        m_atStart = true;
        m_direction = 0;
    }

    void PlaybackCursor::Refit(const ClipData& clip, std::uint32_t revision, ClipLoop loop)
    {
        m_revision = revision;
        m_duration = clip.duration;
        m_time = m_finished ? (m_finishedAtEnd ? m_duration : 0.0) : Fit(m_time, m_duration, loop);
    }

    void PlaybackCursor::Seek(double seconds, ClipLoop loop)
    {
        m_time = Fit(seconds, m_duration, loop);
        m_finished = false;
        m_atStart = false;
    }

    CursorStep PlaybackCursor::Step(const ClipData& clip, ClipLoop loop, bool playing, double delta)
    {
        CursorStep step;
        if (loop == ClipLoop::Loop)
        {
            m_finished = false;
        }
        if (m_duration <= 0.0)
        {
            // A clip with no duration holds its first keys: it never wraps, and starting crosses its events once.
            m_time = 0.0;
            if (playing && m_atStart)
            {
                m_atStart = false;
                Crossings crossings;
                crossings.m_first = Span(clip.events, 0.0, true, 0.0, true, false, 0);
                step.crossings = crossings;
                if (loop == ClipLoop::Once && !m_finished)
                {
                    m_finished = true;
                    step.finishedNow = true;
                }
            }
            return step;
        }
        if (delta == 0.0)
        {
            return step;
        }
        const int direction = delta > 0.0 ? 1 : -1;
        if (m_finished && (direction > 0) != m_finishedAtEnd)
        {
            // Playing away from the boundary it finished at.
            m_finished = false;
        }
        step.jumped = m_direction != 0 && m_direction != direction;
        m_direction = direction;

        // The move: where it lands, and the spans it crossed in playback order.
        const double from = m_time;
        const double to = from + delta;
        const bool forward = delta > 0.0;
        const bool start = m_atStart;
        const std::uint64_t loops = m_completedLoops;
        Crossings crossings;
        crossings.m_reverse = !forward;
        crossings.m_from = from;
        crossings.m_duration = m_duration;
        crossings.m_length = std::abs(delta);
        bool reached = false;
        if (loop == ClipLoop::Once)
        {
            const double clamped = std::clamp(to, 0.0, m_duration);
            reached = forward ? clamped >= m_duration : clamped <= 0.0;
            crossings.m_first = forward ? Span(clip.events, from, start, clamped, true, false, loops)
                                        : Span(clip.events, clamped, true, from, start, true, loops);
            m_time = clamped;
        }
        else
        {
            const double lapsCrossed = std::floor(to / m_duration);
            double time = to - lapsCrossed * m_duration;
            std::uint64_t laps = LapCount(std::abs(lapsCrossed));
            if (time >= m_duration)
            {
                // Rounding can land a wrap on the boundary itself: that is the next lap's start.
                time -= m_duration;
                laps = forward ? SaturatingAdd(laps, 1) : laps - 1;
            }
            if (laps == 0)
            {
                crossings.m_first = forward ? Span(clip.events, from, start, time, true, false, loops)
                                            : Span(clip.events, time, true, from, start, true, loops);
            }
            else if (forward)
            {
                crossings.m_first = Span(clip.events, from, start, m_duration, true, false, loops);
                crossings.m_wholeLaps = laps - 1;
                crossings.m_last = Span(clip.events, 0.0, true, time, true, false, SaturatingAdd(loops, laps));
            }
            else
            {
                crossings.m_first = Span(clip.events, 0.0, true, from, start, true, loops);
                crossings.m_wholeLaps = laps - 1;
                crossings.m_last = Span(clip.events, time, true, m_duration, true, true, SaturatingAdd(loops, laps));
            }
            m_time = time;
            m_completedLoops = SaturatingAdd(loops, laps);
            step.jumped = step.jumped || laps != 0;
        }
        step.crossings = crossings;
        m_atStart = false;
        if (reached && !m_finished)
        {
            m_finished = true;
            m_finishedAtEnd = direction > 0;
            step.finishedNow = true;
        }
        return step;
    }
} // namespace Fenrir::Detail
