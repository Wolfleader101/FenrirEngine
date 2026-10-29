#pragma once

#include <cstdint>
#include <span>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirAssets/SpawnNodes.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    class Scene;

    /** @brief Whether a skeleton entity's pose can be read (ADR-0031). */
    enum class PoseState : std::uint8_t
    {
        /** @brief Its skeleton asset resolves and its pose is built: reads give the current pose. */
        Ready,
        /** @brief Its `SkeletonPose` names an empty or stale skeleton: reads are empty until it resolves. */
        Unavailable,
        /** @brief It is dead, or has no `SkeletonPose`. */
        Missing,
    };

    /**
     * @brief How the renderer reaches skeleton poses without knowing FenrirSkeleton (ADR-0041). An interface holding
     * no state; FenrirSkeleton implements it and registers its implementation as an app resource at install, and the
     * renderer looks it up whenever it needs one, so install order does not matter. With none registered, rigid meshes
     * still draw and skinned draws are skipped.
     *
     * Every call names a scene and a skeleton entity in it, and is main thread only. Spans are valid until the end of
     * the current system or the next change to that scene's skeleton instances.
     */
    class SkinningSource
    {
      public:
        virtual ~SkinningSource() = default;

        /** @brief Whether `skeleton`'s pose can be read. */
        [[nodiscard]] virtual PoseState GetPoseState(Scene& scene, Entity skeleton) = 0;

        /** @brief The skeleton asset `skeleton` instances; empty when it is `Missing`. */
        [[nodiscard]] virtual AssetHandle<Skeleton> GetSkeleton(Scene& scene, Entity skeleton) = 0;

        /**
         * @brief The pose generation (ADR-0035): a number that moves on whenever the final pose changes or the instance
         * is rebuilt, and never repeats within the scene. Caches of the palette are keyed by it. 0 when not `Ready`.
         */
        [[nodiscard]] virtual std::uint64_t GetPoseGeneration(Scene& scene, Entity skeleton) = 0;

        /**
         * @brief The skinning palette, one matrix per bone (model × inverse bind), built if this generation has none
         * yet. Empty when not `Ready`.
         */
        [[nodiscard]] virtual std::span<const Math::Mat4> GetPalette(Scene& scene, Entity skeleton) = 0;

        /** @brief The final pose's model-space matrices, one per bone. Empty when not `Ready`. */
        [[nodiscard]] virtual std::span<const Math::Mat4> GetModelMatrices(Scene& scene, Entity skeleton) = 0;

        /**
         * @brief Gives the spawned nodes of `model` their skeletons: a `SkeletonPose` on each skeleton's owner whose
         * skeleton loaded, and bone attachments for the ordinary nodes below bones (FenrirSkeleton's
         * `Skeletons::SpawnSkeletons`). The renderer's `SpawnModel` calls it between spawning nodes and adding meshes.
         */
        virtual void SpawnSkeletons(Scene& scene, AssetHandle<Model> model, const SpawnedNodes& spawned) = 0;

      protected:
        SkinningSource() = default;
        SkinningSource(const SkinningSource&) = default;
        SkinningSource& operator=(const SkinningSource&) = default;
    };

    /**
     * @brief The app resource the skinning source is registered as: `app.TryGetResource<SkinningSourceRegistration>()`
     * finds it, and `source` is the implementation, owned by whoever registered it (FenrirSkeleton's `Skeletons`
     * owner, which removes the resource when it is destroyed). No resource means no skinning source.
     */
    struct SkinningSourceRegistration
    {
        /** @brief The registered implementation; never null while the resource exists. */
        SkinningSource* source = nullptr;
    };
} // namespace Fenrir
