#include "Sampling.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/quaternion.hpp>

namespace Fenrir::Detail
{
    namespace
    {
        Math::Quat ToQuat(const Math::Vec4& v)
        {
            return Math::Quat(v.w, v.x, v.y, v.z);
        }

        Math::Vec4 FromQuat(const Math::Quat& q)
        {
            return {q.x, q.y, q.z, q.w};
        }

        Math::Vec4 Normalised(const Math::Vec4& v)
        {
            const float length = glm::length(v);
            return length > 0.0f ? v / length : Math::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
        }

        // The key at or before `time`, which lies strictly between the first and last keys' times.
        std::size_t FindKey(const std::vector<float>& times, float time, KeyCursor& cursor)
        {
            const std::size_t last = times.size() - 1;
            if (cursor.search || cursor.key >= last || times[cursor.key] > time)
            {
                const auto after = std::ranges::upper_bound(times, time);
                cursor.key = static_cast<std::size_t>(after - times.begin()) - 1;
                cursor.search = false;
                return cursor.key;
            }
            // Forward playback: the key moves on by a few at most, so walk rather than search.
            while (cursor.key + 1 < last && times[cursor.key + 1] <= time)
            {
                ++cursor.key;
            }
            return cursor.key;
        }
    } // namespace

    Math::Vec4 SampleChannel(const ClipChannel& channel, float time, KeyCursor& cursor)
    {
        const std::vector<float>& times = channel.times;
        const bool rotation = channel.path == ClipPath::Rotation;
        if (times.size() == 1 || time <= times.front())
        {
            cursor.key = 0;
            return channel.values.front();
        }
        if (time >= times.back())
        {
            cursor.key = times.size() - 1;
            return channel.values.back();
        }
        const std::size_t k = FindKey(times, time, cursor);
        const Math::Vec4& from = channel.values[k];
        const Math::Vec4& to = channel.values[k + 1];
        const float interval = times[k + 1] - times[k];
        const float u = (time - times[k]) / interval;
        switch (channel.interpolation)
        {
        case ClipInterpolation::Step:
            return from;
        case ClipInterpolation::Linear:
            if (rotation)
            {
                // glm's slerp takes the shorter arc itself, negating the far key when the dot is negative.
                return FromQuat(glm::slerp(ToQuat(from), ToQuat(to), u));
            }
            return glm::mix(from, to, u);
        case ClipInterpolation::CubicSpline: {
            // glTF's Hermite form: the out tangent of key k and the in tangent of key k + 1, each scaled by the
            // interval between them.
            const float u2 = u * u;
            const float u3 = u2 * u;
            const Math::Vec4 value = (2.0f * u3 - 3.0f * u2 + 1.0f) * from +
                                     (u3 - 2.0f * u2 + u) * interval * channel.outTangents[k] +
                                     (-2.0f * u3 + 3.0f * u2) * to + (u3 - u2) * interval * channel.inTangents[k + 1];
            return rotation ? Normalised(value) : value;
        }
        }
        return from;
    }

    void ApplySample(ClipPath path, const Math::Vec4& value, Transform& local)
    {
        switch (path)
        {
        case ClipPath::Translation:
            local.pos = Math::Vec3(value);
            break;
        case ClipPath::Rotation:
            local.rot = glm::normalize(ToQuat(value));
            break;
        case ClipPath::Scale:
            local.scale = Math::Vec3(value);
            break;
        }
    }
} // namespace Fenrir::Detail
