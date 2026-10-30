#include "Blending.hpp"

#include <cmath>
#include <format>
#include <string>

#include <glm/gtc/quaternion.hpp>

#include "FenrirAssets/BoneMaskData.hpp"
#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "Sampling.hpp"

namespace Fenrir::Detail
{
    namespace
    {
        const Math::Quat Identity(1.0f, 0.0f, 0.0f, 0.0f);

        // How many bones have `name` as their source name: a mask name that is no engine name but several source
        // names is ambiguous.
        std::size_t SourceNameCount(const SkeletonData& skeleton, const std::string& name)
        {
            std::size_t count = 0;
            for (const SkeletonBone& bone : skeleton.bones)
            {
                count += bone.sourceName == name ? 1 : 0;
            }
            return count;
        }
    } // namespace

    std::uint8_t CoverOf(ClipPath path) noexcept
    {
        switch (path)
        {
        case ClipPath::Translation:
            return CoverTranslation;
        case ClipPath::Rotation:
            return CoverRotation;
        case ClipPath::Scale:
            return CoverScale;
        }
        return 0;
    }

    Math::Quat Nlerp(const Math::Quat& a, const Math::Quat& b, float t) noexcept
    {
        const Math::Quat far = glm::dot(a, b) < 0.0f ? -b : b;
        const Math::Quat mixed = a * (1.0f - t) + far * t;
        const float length = glm::length(mixed);
        return length > 0.0f ? mixed / length : a;
    }

    void BlendChannels(Transform& a, const Transform& b, std::uint8_t covered, float t) noexcept
    {
        if ((covered & CoverTranslation) != 0)
        {
            a.pos = glm::mix(a.pos, b.pos, t);
        }
        if ((covered & CoverRotation) != 0)
        {
            a.rot = Nlerp(a.rot, b.rot, t);
        }
        if ((covered & CoverScale) != 0)
        {
            a.scale = glm::mix(a.scale, b.scale, t);
        }
    }

    void ApplyOverride(Transform& current, const Transform& value, std::uint8_t covered, float w) noexcept
    {
        BlendChannels(current, value, covered, w);
    }

    void ApplyAdditive(Transform& current, const Transform& delta, std::uint8_t covered, float w) noexcept
    {
        if ((covered & CoverTranslation) != 0)
        {
            current.pos += delta.pos * w;
        }
        if ((covered & CoverRotation) != 0)
        {
            current.rot = glm::normalize(glm::slerp(Identity, delta.rot, w) * current.rot);
        }
        if ((covered & CoverScale) != 0)
        {
            current.scale *= glm::mix(Math::Vec3(1.0f), delta.scale, w);
        }
    }

    Math::Vec3 ScaleRatio(const Math::Vec3& sample, const Math::Vec3& reference, bool& degenerate) noexcept
    {
        Math::Vec3 ratio(1.0f);
        for (int i = 0; i < 3; ++i)
        {
            const float s = sample[i];
            const float r = reference[i];
            const float value = std::isfinite(s) && std::isfinite(r) && std::abs(r) >= 1e-6f ? s / r : 0.0f;
            if (std::isfinite(value) && value > 0.0f)
            {
                ratio[i] = value;
            }
            else
            {
                degenerate = true;
            }
        }
        return ratio;
    }

    const MaskBinding& MaskBindings::Get(ILogger& logger, AssetHandle<BoneMask> mask, std::uint32_t maskRevision,
                                         const BoneMaskData& data, AssetHandle<Skeleton> skeleton,
                                         std::uint32_t skeletonRevision, const SkeletonData& bones)
    {
        const PairKey key{
            mask.index, mask.generation, maskRevision, skeleton.index, skeleton.generation, skeletonRevision, 0};
        auto [found, made] = m_masks.try_emplace(key);
        MaskBinding& binding = found->second;
        binding.used = true;
        if (!made)
        {
            return binding;
        }
        // Later entries override earlier ones for the bones they reach; unlisted bones stay at 0 (ADR-0036).
        binding.weights.assign(bones.BoneCount(), 0.0f);
        for (const BoneMaskEntry& entry : data.entries)
        {
            const std::int32_t bone = bones.FindBone(entry.boneName);
            if (bone == NoBone)
            {
                const char* reason = SourceNameCount(bones, entry.boneName) > 1
                                         ? "it matches several bones' source names, so it is ambiguous"
                                         : "no bone has that engine name";
                logger.Warn("{}",
                            std::format("FenrirAnimation: bone mask {}.{} (revision {}) names '{}', which selects "
                                        "nothing on skeleton {}.{}: {}",
                                        mask.index, mask.generation, maskRevision, entry.boneName, skeleton.index,
                                        skeleton.generation, reason));
                continue;
            }
            if (entry.scope == MaskScope::Subtree)
            {
                for (const std::int32_t below : bones.Subtree(bone))
                {
                    binding.weights[static_cast<std::size_t>(below)] = entry.weight;
                }
            }
            else
            {
                binding.weights[static_cast<std::size_t>(bone)] = entry.weight;
            }
        }
        return binding;
    }

    void MaskBindings::SweepUnused()
    {
        std::erase_if(m_masks, [](auto& entry) {
            const bool unused = !entry.second.used;
            entry.second.used = false;
            return unused;
        });
    }

    ReferencePose& ReferencePoses::Get(const BindingKey& key, AdditiveReference mode, const ClipData& clip,
                                       const Binding& binding, const SkeletonData& skeleton)
    {
        const PairKey pair{key.clip.index,
                           key.clip.generation,
                           key.clipRevision,
                           key.skeleton.index,
                           key.skeleton.generation,
                           key.skeletonRevision,
                           static_cast<std::uint8_t>(mode)};
        auto [found, made] = m_poses.try_emplace(pair);
        ReferencePose& reference = found->second;
        reference.used = true;
        if (!made)
        {
            return reference;
        }
        // A complete local pose: rest, and for ClipStart the clip's keys at 0 where it animates (ADR-0036).
        reference.pose.resize(skeleton.BoneCount());
        reference.reportedScale.assign(skeleton.BoneCount(), false);
        for (std::size_t i = 0; i < skeleton.BoneCount(); ++i)
        {
            reference.pose[i] = skeleton.bones[i].rest;
        }
        if (mode == AdditiveReference::ClipStart)
        {
            for (std::size_t c = 0; c < clip.channels.size(); ++c)
            {
                const std::int32_t bone = binding.bones[c];
                if (bone != NoBone)
                {
                    KeyCursor cursor;
                    ApplySample(clip.channels[c].path, SampleChannel(clip.channels[c], 0.0f, cursor),
                                reference.pose[static_cast<std::size_t>(bone)]);
                }
            }
        }
        return reference;
    }

    void ReferencePoses::SweepUnused()
    {
        std::erase_if(m_poses, [](auto& entry) {
            const bool unused = !entry.second.used;
            entry.second.used = false;
            return unused;
        });
    }
} // namespace Fenrir::Detail
