#pragma once

#include <cstddef>

#include "FenrirAssets/ClipData.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir::Detail
{
    /**
     * @brief Where one channel's last sample was among its keys: the key at or before that time. Forward playback
     * moves it on without searching; `search` asks the next sample to find its key by binary search, as after a seek,
     * a reversal or a wrap (ADR-0034).
     */
    struct KeyCursor
    {
        std::size_t key = 0;
        bool search = true;
    };

    /**
     * @brief The channel's value at `time` as glTF defines it: `Step` holds a key, `Linear` lerps (rotations slerp
     * along the shortest path), `CubicSpline` is the Hermite form with tangents scaled by the key interval, rotations
     * normalised after. Before the first key its value holds, after the last key the last value. A rotation comes back
     * as (x, y, z, w).
     */
    [[nodiscard]] Math::Vec4 SampleChannel(const ClipChannel& channel, float time, KeyCursor& cursor);

    /** @brief Writes a sampled `value` into the part of `local` that `path` names. */
    void ApplySample(ClipPath path, const Math::Vec4& value, Transform& local);
} // namespace Fenrir::Detail
