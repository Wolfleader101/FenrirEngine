#pragma once

#include <compare>
#include <cstdint>
#include <map>
#include <tuple>
#include <vector>

#include "FenrirAssets/AssetHandle.hpp"

namespace Fenrir
{
    class AssetStore;
    class ILogger;
    struct ClipData;
    struct SkeletonData;

    namespace Detail
    {
        /** @brief What a binding is for: a clip and a skeleton, each at one revision (ADR-0034). */
        struct BindingKey
        {
            AssetHandle<Clip> clip;
            std::uint32_t clipRevision = 0;
            AssetHandle<Skeleton> skeleton;
            std::uint32_t skeletonRevision = 0;

            friend auto operator<=>(const BindingKey& a, const BindingKey& b) noexcept
            {
                return std::tie(a.clip.index, a.clip.generation, a.clipRevision, a.skeleton.index,
                                a.skeleton.generation, a.skeletonRevision) <=>
                       std::tie(b.clip.index, b.clip.generation, b.clipRevision, b.skeleton.index,
                                b.skeleton.generation, b.skeletonRevision);
            }
            friend bool operator==(const BindingKey&, const BindingKey&) = default;
        };

        /** @brief A binding: each of the clip's channels' bone, or `NoBone` for a channel that is not played. */
        struct Binding
        {
            std::vector<std::int32_t> bones;
            // Whether an instance used it in the current pass; one that none did is dropped after the pass.
            bool used = false;
        };

        /**
         * @brief The bindings in use, shared by every instance of a (clip, skeleton) pair and dropped when none uses
         * them. Numeric only: sampling never looks a name up.
         */
        class Bindings
        {
          public:
            /** @brief The pair's binding, built (and its unbound channels logged) the first time it is asked for. */
            const Binding& Get(const AssetStore& store, ILogger& logger, const BindingKey& key, const ClipData& clip,
                               const SkeletonData& skeleton);

            /** @brief Drops every binding no instance asked for since the last sweep. */
            void SweepUnused();

          private:
            std::map<BindingKey, Binding> m_bindings;
        };
    } // namespace Detail
} // namespace Fenrir
