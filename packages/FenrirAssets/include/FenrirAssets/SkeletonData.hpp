#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    struct SkeletonData;

    namespace Detail
    {
        [[nodiscard]] std::string PrepareAsset(SkeletonData& skeleton, std::vector<std::string>& warnings);
    } // namespace Detail

    /** @brief No bone: what a bone query gives when there is none (a root's parent, a name that matches nothing). */
    inline constexpr std::int32_t NoBone = -1;

    /** @brief The most bones a skeleton holds: bone indices are stored as 16 bits, and one value is kept for "none". */
    inline constexpr std::size_t MaxBones = 65535;

    /** @brief The longest engine name a bone gets, in UTF-8 bytes, so it fits a `BoneName` (ADR-0037). */
    inline constexpr std::size_t MaxBoneNameBytes = 255;

    /**
     * @brief `bone` and every bone below it, in index order (so each after its parent), over `count` bones in a
     * parents-first order whose parents `parentOf(i)` gives (`NoBone` for a root). Empty when `bone` is out of range.
     * `SkeletonData::Subtree` walks its own parents with it; a skeleton instance walks its copy of them.
     */
    template <typename ParentOf>
    [[nodiscard]] std::vector<std::int32_t> SubtreeOf(std::size_t count, std::int32_t bone, ParentOf parentOf)
    {
        if (bone < 0 || static_cast<std::size_t>(bone) >= count)
        {
            return {};
        }
        // Parents come first, so one pass from the bone onwards finds every bone below it.
        std::vector<bool> inside(count, false);
        inside[static_cast<std::size_t>(bone)] = true;
        std::vector<std::int32_t> subtree{bone};
        for (std::size_t i = static_cast<std::size_t>(bone) + 1; i < count; ++i)
        {
            const std::int32_t parent = parentOf(i);
            if (parent != NoBone && inside[static_cast<std::size_t>(parent)])
            {
                inside[i] = true;
                subtree.push_back(static_cast<std::int32_t>(i));
            }
        }
        return subtree;
    }

    /**
     * @brief What matching a bone by a name from elsewhere found (`SkeletonData::MatchBoneName`): a bone, no bone with
     * that engine name, or a bone whose source name the skeleton repeats, so the name may mean another bone.
     */
    struct BoneNameMatch
    {
        /** @brief How the match ended. */
        enum class Outcome : std::uint8_t
        {
            /** @brief `bone` is the one bone the name means. */
            Matched,
            /** @brief No bone has that engine name. */
            NoMatch,
            /** @brief The bone with that engine name shares its source name with another bone. */
            Ambiguous,
        };

        /** @brief How the match ended. */
        Outcome outcome = Outcome::NoMatch;
        /** @brief The bone when `outcome` is `Matched`, else `NoBone`. */
        std::int32_t bone = NoBone;
    };

    /** @brief One bone of a skeleton, as its file (or the code building it) gives it. */
    struct SkeletonBone
    {
        /** @brief The bone's name in its file. It may be empty, and need not be unique; lookups use the engine name. */
        std::string sourceName;
        /**
         * @brief The bone's node identity in its file (the model node's `sourceId`), when it came from one. Clips from
         * the same file bind by it before falling back to names (ADR-0031). No two bones of a skeleton share one.
         */
        std::optional<std::uint64_t> sourceId;
        /** @brief The bone's node in its model's `Model::nodes`, or -1 for a skeleton built in code. */
        std::int32_t modelNode = -1;
        /** @brief The parent bone's index, lower than this bone's own; `NoBone` for a root. Several roots are allowed.
         */
        std::int32_t parent = NoBone;
        /** @brief The rest pose: the bone's transform relative to its parent as authored, where an instance starts. */
        Transform rest;
        /**
         * @brief The inverse bind: from skeleton bind space into the bone's space at the bind pose, column-major and
         * affine. Distinct from the rest pose, which need not match the bind pose.
         */
        Math::Mat4 inverseBind = Math::Mat4(1.0f);
    };

    /**
     * @brief A skeleton as plain data (ADR-0028, ADR-0031, ADR-0035): its bones in a parents-first order, each with its
     * rest pose and inverse bind, imported from a model or built in code. Skeleton instances (FenrirSkeleton's
     * `SkeletonPose`) are posed; this never changes once the store holds it.
     *
     * Fill `bones` and add it with `AssetStore::Add<Skeleton>`. The store checks the bones as the importer does
     * (finite, non-degenerate rest transforms and inverse binds, each parent before its child, no node used twice) and
     * refuses a skeleton that breaks the rules. It then derives what the queries read: each bone's unique engine name,
     * the name lookups and the bind signature. The queries answer nothing useful before that.
     */
    struct SkeletonData
    {
        /** @brief Every bone, each after its parent. Bone indices are positions in this list. */
        std::vector<SkeletonBone> bones;

        /** @brief How many bones the skeleton has. */
        [[nodiscard]] std::size_t BoneCount() const noexcept
        {
            return bones.size();
        }

        /** @brief The bone's parent, or `NoBone` for a root or a bone out of range. */
        [[nodiscard]] std::int32_t Parent(std::int32_t bone) const noexcept;

        /**
         * @brief The bone's engine name: its source name, made unique by a `#2`, `#3`... suffix on a repeat, and cut
         * at a UTF-8 boundary then suffixed when longer than `MaxBoneNameBytes`. Empty for a bone out of range.
         */
        [[nodiscard]] std::string_view EngineName(std::int32_t bone) const noexcept;

        /** @brief The bone whose engine name is exactly `name`, or `NoBone`. */
        [[nodiscard]] std::int32_t FindBone(std::string_view name) const;

        /**
         * @brief Matches a bone by a name from elsewhere, a clip channel's or a bone mask entry's (ADR-0031, ADR-0036):
         * the bone whose engine name is `name`, unless its source name is another bone's too. A `#2` suffix comes from
         * this skeleton's own import order, so a name from another file cannot be trusted to mean the same repeat:
         * every bone of a repeated source name is `Ambiguous`, suffixed or not. `FindBone` stays exact.
         */
        [[nodiscard]] BoneNameMatch MatchBoneName(std::string_view name) const;

        /** @brief The first bone, in index order, whose source name is `name`, or `NoBone`. */
        [[nodiscard]] std::int32_t FindBoneBySourceName(std::string_view name) const;

        /** @brief Whether `bone` lies below `ancestor`: its parent, or its parent's parent, and so on. A bone is not
         * its own descendant. False when either is out of range. */
        [[nodiscard]] bool IsDescendant(std::int32_t bone, std::int32_t ancestor) const noexcept;

        /** @brief The bone and every bone below it, in index order (so each after its parent); empty when out of range.
         */
        [[nodiscard]] std::vector<std::int32_t> Subtree(std::int32_t bone) const;

        /**
         * @brief The bind signature (ADR-0035): a versioned FNV-1a 64 hash of the bone count, parents, engine names and
         * inverse binds. A skinned mesh baked against this skeleton records it as its `expectedSignature`. Never 0,
         * which means "unchecked", once the store holds the skeleton.
         */
        [[nodiscard]] std::uint64_t BindSignature() const noexcept
        {
            return m_bindSignature;
        }

      private:
        friend std::string Detail::PrepareAsset(SkeletonData& skeleton, std::vector<std::string>& warnings);

        // Derived by the store from `bones`, never stored in a file.
        std::vector<std::string> m_engineNames;
        std::vector<std::uint16_t> m_parents;
        std::unordered_map<std::string, std::uint16_t> m_byEngineName;
        std::unordered_map<std::string, std::uint16_t> m_bySourceName;
        // Per bone: 1 when another bone has the same source name, so a match by name is ambiguous.
        std::vector<std::uint8_t> m_sharesSourceName;
        std::uint64_t m_bindSignature = 0;
    };

    /**
     * @brief The bind signature of `bones` as they are, with the engine names given (ADR-0035's encoding, version 1).
     * The store sets a skeleton's signature with this; it is public so that tests and tools can check one.
     */
    [[nodiscard]] std::uint64_t ComputeBindSignature(const std::vector<SkeletonBone>& bones,
                                                     const std::vector<std::string>& engineNames);
} // namespace Fenrir
