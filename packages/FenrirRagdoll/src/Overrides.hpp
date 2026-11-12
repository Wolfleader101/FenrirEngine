#pragma once

// The pose override entries the glue wrote, per skeleton, kept as a resource of the skeleton's scene (so an inactive
// scene keeps its own, and entity ids of two scenes never meet): the clearing rule reads their weights, and
// Ragdolls::ClearOverrides and DestroyRagdoll clear them, including the bones of bodies destroyed since. Private to
// FenrirRagdoll.

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "FenrirECS/Entity.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirSkeleton/Skeletons.hpp"

namespace Fenrir::Detail
{
    /** @brief The bones the glue wrote an entry for on one skeleton entity, in the order it first wrote them. */
    struct WrittenBones
    {
        Entity skeleton;
        std::vector<std::int32_t> bones;

        void Add(std::int32_t bone)
        {
            if (std::find(bones.begin(), bones.end(), bone) == bones.end())
            {
                bones.push_back(bone);
            }
        }
    };

    /** @brief A scene's written bones, by skeleton entity id. */
    struct RagdollOverrides
    {
        std::unordered_map<std::uint32_t, WrittenBones> bySkeleton;

        /** @brief `skeleton`'s record, or nullptr; a record left by an earlier entity of the same id is dropped. */
        WrittenBones* Find(const Entity& skeleton)
        {
            const auto found = bySkeleton.find(skeleton.GetId());
            if (found == bySkeleton.end())
            {
                return nullptr;
            }
            if (found->second.skeleton != skeleton)
            {
                bySkeleton.erase(found);
                return nullptr;
            }
            return &found->second;
        }

        /** @brief `skeleton`'s record, made empty if it has none. */
        WrittenBones& Of(const Entity& skeleton)
        {
            if (WrittenBones* found = Find(skeleton))
            {
                return *found;
            }
            WrittenBones& added = bySkeleton[skeleton.GetId()];
            added.skeleton = skeleton;
            return added;
        }

        /** @brief Clears every entry `skeleton`'s record names, and the record. */
        void Clear(Scene& scene, const Entity& skeleton)
        {
            if (const WrittenBones* written = Find(skeleton))
            {
                for (const std::int32_t bone : written->bones)
                {
                    Skeletons::ClearOverride(scene, skeleton, bone);
                }
                bySkeleton.erase(skeleton.GetId());
            }
        }

        /** @brief Drops the records of skeletons `keep` rejects. */
        template <typename Keep>
        void Prune(Keep keep)
        {
            std::erase_if(bySkeleton, [&keep](const auto& entry) { return !keep(entry.second.skeleton); });
        }

        /** @brief The scene's record, made on first use. */
        static RagdollOverrides& In(Scene& scene)
        {
            if (RagdollOverrides* found = scene.GetResource<RagdollOverrides>())
            {
                return *found;
            }
            return scene.AddResource<RagdollOverrides>();
        }
    };
} // namespace Fenrir::Detail
