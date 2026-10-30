#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    /**
     * @brief A short number for a clip event's name: the 32-bit FNV-1a hash of its UTF-8 bytes. A convenience for
     * comparing names cheaply; an event is identified by its index and its clip's revision, never by this.
     */
    struct ClipEventId
    {
        std::uint32_t value = 0;

        /** @brief The id of `name`. C# computes the same number (`ClipEventId.Of`). */
        [[nodiscard]] static constexpr ClipEventId Of(std::string_view name) noexcept
        {
            std::uint32_t hash = 2166136261u;
            for (const char c : name)
            {
                hash ^= static_cast<std::uint8_t>(c);
                hash *= 16777619u;
            }
            return {hash};
        }

        friend constexpr bool operator==(ClipEventId, ClipEventId) = default;
    };

    /** @brief Which part of a bone's transform a clip channel keys. */
    enum class ClipPath : std::uint8_t
    {
        Translation,
        Rotation,
        Scale,
    };

    /** @brief How a clip channel's values run between its keys (glTF's three). */
    enum class ClipInterpolation : std::uint8_t
    {
        /** @brief Each key's value holds until the next key. */
        Step,
        /** @brief Values lerp between keys, rotations by shortest-path slerp. */
        Linear,
        /** @brief glTF's cubic Hermite spline, with an in and an out tangent per key. */
        CubicSpline,
    };

    /** @brief One keyed part of one bone's transform over time. */
    struct ClipChannel
    {
        /**
         * @brief The target node's identity in the clip's own file (its model node's `sourceId`), when it came from
         * one: a skeleton from the same file binds by it first (ADR-0031).
         */
        std::optional<std::uint64_t> targetSourceId;
        /** @brief The target bone's engine name, which every other skeleton binds by. */
        std::string targetName;
        /**
         * @brief Whether the target is a bone. A channel on an ordinary node of its file is kept but marked unplayable
         * (false): node animation is out of scope, and the channel is never redirected into a skeleton (ADR-0031).
         */
        bool playable = true;
        /** @brief What the channel keys. */
        ClipPath path = ClipPath::Translation;
        /** @brief How it runs between keys. */
        ClipInterpolation interpolation = ClipInterpolation::Linear;
        /** @brief The authored key times in seconds: finite, non-negative and strictly increasing, at least one. */
        std::vector<float> times;
        /**
         * @brief One value per key: a translation or scale in `xyz` (`w` unused), or a rotation as a quaternion in
         * `(x, y, z, w)`, glTF's order.
         */
        std::vector<Math::Vec4> values;
        /** @brief One in tangent per key for `CubicSpline`, as authored (glTF's, before scaling by the interval). Empty
         * otherwise. */
        std::vector<Math::Vec4> inTangents;
        /** @brief One out tangent per key for `CubicSpline`; empty otherwise. */
        std::vector<Math::Vec4> outTangents;
    };

    /** @brief A named moment in a clip, sent as an animation event when playback crosses it (ADR-0034). */
    struct ClipEvent
    {
        /** @brief When it happens, in seconds, within the clip's duration. */
        float time = 0.0f;
        /** @brief Its name, which need not be unique. */
        std::string name;
    };

    /**
     * @brief A clip as plain data (ADR-0034): keyed bone transforms over time, and timed events.
     *
     * Add it with `AssetStore::Add<Clip>`. The store checks the channels (keys finite and in order, one value per key,
     * tangents only and always for cubic keys), sets the duration, and sorts the events by time, equal times keeping
     * the order they were given in. Events are then edited through the store (`AddClipEvent`, `RemoveClipEvent`), each
     * edit bumping the clip's revision.
     */
    struct ClipData
    {
        /** @brief The keyed channels. */
        std::vector<ClipChannel> channels;
        /** @brief The timed events, by time. */
        std::vector<ClipEvent> events;
        /**
         * @brief How long the clip plays: the last key's time over every channel, 0 with no channels. The store
         * computes it whenever the clip is added or replaced, overwriting what was here.
         */
        float duration = 0.0f;

        /** @brief The index of every event named `name`, in time order. */
        [[nodiscard]] std::vector<std::uint32_t> FindEvents(std::string_view name) const;
    };
} // namespace Fenrir
