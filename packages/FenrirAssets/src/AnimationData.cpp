// The animation spec's asset data (ADR-0031, ADR-0034, ADR-0035, ADR-0036): the checks the store runs before taking a
// skeleton, clip or bone mask, what it derives from them, and their queries.
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/BoneMaskData.hpp"
#include "FenrirAssets/ClipData.hpp"
#include "FenrirAssets/SkeletonData.hpp"

namespace Fenrir
{
    namespace
    {
        // Below this a scale component, or a matrix's determinant, is degenerate: nothing can be undone through it.
        constexpr float DegenerateScale = 1e-8f;
        constexpr double DegenerateDeterminant = 1e-12;

        constexpr std::uint16_t NoBoneStored = 0xFFFF;

        bool Finite(const Math::Vec3& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        bool Finite(const Math::Vec4& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w);
        }

        bool Finite(const Math::Mat4& m)
        {
            for (glm::length_t c = 0; c < 4; ++c)
            {
                if (!Finite(m[c]))
                {
                    return false;
                }
            }
            return true;
        }

        // `text` cut to at most `bytes` bytes, never inside a UTF-8 sequence.
        std::string_view Utf8Prefix(std::string_view text, std::size_t bytes)
        {
            if (text.size() <= bytes)
            {
                return text;
            }
            std::size_t cut = bytes;
            while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0u) == 0x80u)
            {
                --cut;
            }
            return text.substr(0, cut);
        }

        // The engine name for a source name: kept when unique and short enough, else cut and suffixed `#2`, `#3`...
        // until no earlier bone has it.
        std::string EngineNameFor(std::size_t bone, const std::string& source,
                                  const std::unordered_set<std::string>& taken, std::vector<std::string>& warnings)
        {
            const bool tooLong = source.size() > MaxBoneNameBytes;
            if (!tooLong && !taken.contains(source))
            {
                return source;
            }
            for (std::size_t n = 2;; ++n)
            {
                const std::string suffix = std::format("#{}", n);
                std::string candidate = std::string(Utf8Prefix(source, MaxBoneNameBytes - suffix.size())) + suffix;
                if (!taken.contains(candidate))
                {
                    if (tooLong)
                    {
                        warnings.push_back(std::format("bone {}'s name is {} bytes, over the {} a bone name may "
                                                       "have, so it is named '{}'",
                                                       bone, source.size(), MaxBoneNameBytes, candidate));
                    }
                    else
                    {
                        warnings.push_back(std::format("bone {}'s name '{}' is an earlier bone's too, so it is "
                                                       "named '{}'",
                                                       bone, source, candidate));
                    }
                    return candidate;
                }
            }
        }

        // FNV-1a 64 over little-endian bytes (ADR-0035).
        class Fnv64
        {
          public:
            void Byte(std::uint8_t byte) noexcept
            {
                m_hash ^= byte;
                m_hash *= 1099511628211ull;
            }
            void U32(std::uint32_t value) noexcept
            {
                for (int shift = 0; shift < 32; shift += 8)
                {
                    Byte(static_cast<std::uint8_t>(value >> shift));
                }
            }
            void Bytes(std::string_view text) noexcept
            {
                for (const char c : text)
                {
                    Byte(static_cast<std::uint8_t>(c));
                }
            }
            [[nodiscard]] std::uint64_t Value() const noexcept
            {
                return m_hash;
            }

          private:
            std::uint64_t m_hash = 14695981039346656037ull;
        };

        constexpr std::uint32_t BindSignatureVersion = 1;
    } // namespace

    std::uint64_t ComputeBindSignature(const std::vector<SkeletonBone>& bones,
                                       const std::vector<std::string>& engineNames)
    {
        Fnv64 hash;
        hash.U32(BindSignatureVersion);
        hash.U32(static_cast<std::uint32_t>(bones.size()));
        for (std::size_t i = 0; i < bones.size(); ++i)
        {
            hash.U32(static_cast<std::uint32_t>(bones[i].parent));
            const std::string_view name =
                i < engineNames.size() ? std::string_view(engineNames[i]) : std::string_view();
            hash.U32(static_cast<std::uint32_t>(name.size()));
            hash.Bytes(name);
            const Math::Mat4& inverseBind = bones[i].inverseBind;
            for (glm::length_t column = 0; column < 4; ++column)
            {
                for (glm::length_t row = 0; row < 4; ++row)
                {
                    const float value = inverseBind[column][row];
                    // -0 and +0 are one value, so they hash alike.
                    hash.U32(value == 0.0f ? 0u : std::bit_cast<std::uint32_t>(value));
                }
            }
        }
        // 0 means "unchecked", so no skeleton has it.
        return hash.Value() == 0 ? 1 : hash.Value();
    }

    std::int32_t SkeletonData::Parent(std::int32_t bone) const noexcept
    {
        if (bone < 0 || static_cast<std::size_t>(bone) >= m_parents.size())
        {
            return NoBone;
        }
        const std::uint16_t parent = m_parents[static_cast<std::size_t>(bone)];
        return parent == NoBoneStored ? NoBone : std::int32_t{parent};
    }

    std::string_view SkeletonData::EngineName(std::int32_t bone) const noexcept
    {
        if (bone < 0 || static_cast<std::size_t>(bone) >= m_engineNames.size())
        {
            return {};
        }
        return m_engineNames[static_cast<std::size_t>(bone)];
    }

    std::int32_t SkeletonData::FindBone(std::string_view name) const
    {
        const auto found = m_byEngineName.find(std::string(name));
        return found == m_byEngineName.end() ? NoBone : std::int32_t{found->second};
    }

    std::int32_t SkeletonData::FindBoneBySourceName(std::string_view name) const
    {
        const auto found = m_bySourceName.find(std::string(name));
        return found == m_bySourceName.end() ? NoBone : std::int32_t{found->second};
    }

    bool SkeletonData::IsDescendant(std::int32_t bone, std::int32_t ancestor) const noexcept
    {
        const std::int32_t count = static_cast<std::int32_t>(m_parents.size());
        if (bone < 0 || bone >= count || ancestor < 0 || ancestor >= count)
        {
            return false;
        }
        // Parents come before children, so the walk up ends at a root.
        for (std::int32_t current = Parent(bone); current != NoBone; current = Parent(current))
        {
            if (current == ancestor)
            {
                return true;
            }
        }
        return false;
    }

    BoneNameMatch SkeletonData::MatchBoneName(std::string_view name) const
    {
        const std::int32_t bone = FindBone(name);
        if (bone == NoBone)
        {
            return {BoneNameMatch::Outcome::NoMatch, NoBone};
        }
        if (m_sharesSourceName[static_cast<std::size_t>(bone)] != 0)
        {
            return {BoneNameMatch::Outcome::Ambiguous, NoBone};
        }
        return {BoneNameMatch::Outcome::Matched, bone};
    }

    std::vector<std::int32_t> SkeletonData::Subtree(std::int32_t bone) const
    {
        return SubtreeOf(m_parents.size(), bone,
                         [this](std::size_t i) { return Parent(static_cast<std::int32_t>(i)); });
    }

    std::vector<std::uint32_t> ClipData::FindEvents(std::string_view name) const
    {
        std::vector<std::uint32_t> found;
        for (std::size_t i = 0; i < events.size(); ++i)
        {
            if (events[i].name == name)
            {
                found.push_back(static_cast<std::uint32_t>(i));
            }
        }
        return found;
    }

    namespace Detail
    {
        std::string PrepareAsset(SkeletonData& skeleton, AssetWarnings& warnings)
        {
            std::vector<SkeletonBone>& bones = skeleton.bones;
            if (bones.empty())
            {
                return "it has no bones, and a skeleton needs at least one";
            }
            if (bones.size() > MaxBones)
            {
                return std::format("it has {} bones, over the {} a skeleton may have", bones.size(), MaxBones);
            }

            std::unordered_set<std::uint64_t> sourceIds;
            std::unordered_set<std::int32_t> modelNodes;
            for (std::size_t i = 0; i < bones.size(); ++i)
            {
                SkeletonBone& bone = bones[i];
                if (bone.parent < NoBone || bone.parent >= static_cast<std::int32_t>(i))
                {
                    return std::format("bone {}'s parent is {}, which is not an earlier bone: parents must come before "
                                       "their children, and cannot form a cycle",
                                       i, bone.parent);
                }
                if (!Finite(bone.rest.pos) || !Finite(bone.rest.scale) ||
                    !Finite(Math::Vec4(bone.rest.rot.x, bone.rest.rot.y, bone.rest.rot.z, bone.rest.rot.w)))
                {
                    return std::format("bone {}'s rest transform is not finite", i);
                }
                if (std::abs(bone.rest.scale.x) < DegenerateScale || std::abs(bone.rest.scale.y) < DegenerateScale ||
                    std::abs(bone.rest.scale.z) < DegenerateScale)
                {
                    return std::format("bone {}'s rest scale ({}, {}, {}) is zero or degenerate", i, bone.rest.scale.x,
                                       bone.rest.scale.y, bone.rest.scale.z);
                }
                const float rotationLength = glm::length(bone.rest.rot);
                if (rotationLength < 1e-6f)
                {
                    return std::format("bone {}'s rest rotation is a zero quaternion", i);
                }
                bone.rest.rot = bone.rest.rot / rotationLength;
                if (!Finite(bone.inverseBind))
                {
                    return std::format("bone {}'s inverse bind is not finite", i);
                }
                if (std::abs(static_cast<double>(glm::determinant(bone.inverseBind))) < DegenerateDeterminant)
                {
                    return std::format("bone {}'s inverse bind is degenerate: it cannot be inverted", i);
                }
                if (bone.sourceId && !sourceIds.insert(*bone.sourceId).second)
                {
                    return std::format("bone {}'s source node {} is an earlier bone's too: a node is one bone at most",
                                       i, *bone.sourceId);
                }
                if (bone.modelNode >= 0 && !modelNodes.insert(bone.modelNode).second)
                {
                    return std::format("bone {}'s model node {} is an earlier bone's too: a node is one bone at most",
                                       i, bone.modelNode);
                }
            }

            skeleton.m_engineNames.clear();
            skeleton.m_parents.clear();
            skeleton.m_byEngineName.clear();
            skeleton.m_bySourceName.clear();
            std::unordered_set<std::string> taken;
            for (std::size_t i = 0; i < bones.size(); ++i)
            {
                std::string engineName = EngineNameFor(i, bones[i].sourceName, taken, warnings);
                taken.insert(engineName);
                const auto index = static_cast<std::uint16_t>(i);
                skeleton.m_byEngineName.emplace(engineName, index);
                // The first bone with a source name keeps it.
                skeleton.m_bySourceName.emplace(bones[i].sourceName, index);
                skeleton.m_engineNames.push_back(std::move(engineName));
                skeleton.m_parents.push_back(bones[i].parent == NoBone ? NoBoneStored
                                                                       : static_cast<std::uint16_t>(bones[i].parent));
            }
            std::unordered_map<std::string_view, std::size_t> sourceNameCounts;
            for (const SkeletonBone& bone : bones)
            {
                ++sourceNameCounts[bone.sourceName];
            }
            skeleton.m_sharesSourceName.clear();
            for (const SkeletonBone& bone : bones)
            {
                skeleton.m_sharesSourceName.push_back(sourceNameCounts[bone.sourceName] > 1 ? 1 : 0);
            }
            skeleton.m_bindSignature = ComputeBindSignature(bones, skeleton.m_engineNames);
            return {};
        }

        std::string PrepareAsset(ClipData& clip, AssetWarnings&)
        {
            float duration = 0.0f;
            for (std::size_t c = 0; c < clip.channels.size(); ++c)
            {
                const ClipChannel& channel = clip.channels[c];
                if (channel.times.empty())
                {
                    return std::format("channel {} has no keys", c);
                }
                if (channel.values.size() != channel.times.size())
                {
                    return std::format("channel {} has {} values for {} keys", c, channel.values.size(),
                                       channel.times.size());
                }
                for (std::size_t k = 0; k < channel.times.size(); ++k)
                {
                    const float time = channel.times[k];
                    if (!std::isfinite(time) || time < 0.0f)
                    {
                        return std::format("channel {}'s key {} is at {}, and key times must be finite and not "
                                           "negative",
                                           c, k, time);
                    }
                    if (k > 0 && time <= channel.times[k - 1])
                    {
                        return std::format("channel {}'s key {} at {} is not after the key before it", c, k, time);
                    }
                    if (!Finite(channel.values[k]))
                    {
                        return std::format("channel {}'s key {} has a value that is not finite", c, k);
                    }
                    if (channel.path == ClipPath::Rotation && glm::length(channel.values[k]) < 1e-6f)
                    {
                        return std::format("channel {}'s key {} is a zero quaternion", c, k);
                    }
                }
                const bool cubic = channel.interpolation == ClipInterpolation::CubicSpline;
                if (cubic && (channel.inTangents.size() != channel.times.size() ||
                              channel.outTangents.size() != channel.times.size()))
                {
                    return std::format("channel {} is cubic, so it needs an in and an out tangent for each of its {} "
                                       "keys",
                                       c, channel.times.size());
                }
                if (!cubic && (!channel.inTangents.empty() || !channel.outTangents.empty()))
                {
                    return std::format("channel {} has tangents, which only cubic keys have", c);
                }
                for (std::size_t k = 0; k < channel.inTangents.size(); ++k)
                {
                    if (!Finite(channel.inTangents[k]) || !Finite(channel.outTangents[k]))
                    {
                        return std::format("channel {}'s key {} has a tangent that is not finite", c, k);
                    }
                }
                duration = std::max(duration, channel.times.back());
            }
            for (std::size_t e = 0; e < clip.events.size(); ++e)
            {
                const float time = clip.events[e].time;
                if (!std::isfinite(time) || time < 0.0f || time > duration)
                {
                    return std::format("event {} ('{}') is at {}, outside the clip's {} seconds", e,
                                       clip.events[e].name, time, duration);
                }
            }
            clip.duration = duration;
            std::stable_sort(clip.events.begin(), clip.events.end(),
                             [](const ClipEvent& a, const ClipEvent& b) { return a.time < b.time; });
            return {};
        }

        std::string PrepareAsset(BoneMaskData& mask, AssetWarnings&)
        {
            for (std::size_t i = 0; i < mask.entries.size(); ++i)
            {
                if (!std::isfinite(mask.entries[i].weight))
                {
                    return std::format("entry {} ('{}') has a weight that is not finite", i, mask.entries[i].boneName);
                }
            }
            return {};
        }
    } // namespace Detail
} // namespace Fenrir
