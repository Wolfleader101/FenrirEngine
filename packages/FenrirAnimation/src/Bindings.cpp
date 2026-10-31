#include "Bindings.hpp"

#include <format>
#include <string>
#include <string_view>

#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/ClipData.hpp"
#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirLogger/ILogger.hpp"

namespace Fenrir::Detail
{
    namespace
    {
        std::string_view PathName(ClipPath path)
        {
            switch (path)
            {
            case ClipPath::Translation:
                return "translation";
            case ClipPath::Rotation:
                return "rotation";
            case ClipPath::Scale:
                return "scale";
            }
            return "?";
        }

        // The channel's bone, or NoBone with why not.
        std::int32_t Bind(const ClipChannel& channel, const SkeletonData& skeleton, bool sameFile,
                          std::string_view& reason)
        {
            if (!channel.playable)
            {
                reason = "its target is an ordinary node, not a bone";
                return NoBone;
            }
            if (sameFile && channel.targetSourceId.has_value())
            {
                // Within the clip's own file, node identity is exact even where names repeat (ADR-0031).
                for (std::size_t i = 0; i < skeleton.bones.size(); ++i)
                {
                    if (skeleton.bones[i].sourceId == channel.targetSourceId)
                    {
                        return static_cast<std::int32_t>(i);
                    }
                }
                reason = "no bone of the skeleton has its node identity";
                return NoBone;
            }
            const BoneNameMatch match = skeleton.MatchBoneName(channel.targetName);
            switch (match.outcome)
            {
            case BoneNameMatch::Outcome::NoMatch:
                reason = "no bone has that engine name";
                break;
            case BoneNameMatch::Outcome::Ambiguous:
                reason = "the match is ambiguous: the skeleton repeats that bone's name";
                break;
            case BoneNameMatch::Outcome::Matched:
                break;
            }
            return match.bone;
        }
    } // namespace

    const Binding& Bindings::Get(const AssetStore& store, ILogger& logger, const BindingKey& key, const ClipData& clip,
                                 const SkeletonData& skeleton)
    {
        auto [found, made] = m_bindings.try_emplace(key);
        Binding& binding = found->second;
        binding.used = true;
        if (!made)
        {
            return binding;
        }
        const bool sameFile = store.FromSameFile(key.clip, key.skeleton);
        binding.bones.resize(clip.channels.size(), NoBone);
        for (std::size_t c = 0; c < clip.channels.size(); ++c)
        {
            const ClipChannel& channel = clip.channels[c];
            std::string_view reason;
            binding.bones[c] = Bind(channel, skeleton, sameFile, reason);
            if (binding.bones[c] == NoBone)
            {
                logger.Warn("{}", std::format("FenrirAnimation: clip {}.{} (revision {}) channel {}, target '{}' {}, "
                                              "is not played: {}",
                                              key.clip.index, key.clip.generation, key.clipRevision, c,
                                              channel.targetName, PathName(channel.path), reason));
            }
        }
        return binding;
    }

    void Bindings::SweepUnused()
    {
        std::erase_if(m_bindings, [](auto& entry) {
            const bool unused = !entry.second.used;
            entry.second.used = false;
            return unused;
        });
    }
} // namespace Fenrir::Detail
