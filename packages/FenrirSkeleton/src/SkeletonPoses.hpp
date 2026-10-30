#pragma once

#include <cstdint>
#include <optional>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Attachments.hpp"
#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirAssets/SkinningSource.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirSkeleton/PoseStages.hpp"
#include "FenrirSkeleton/SkeletonPose.hpp"

namespace Fenrir
{
    class AssetStore;
    class Scene;
    struct SkeletonData;

    namespace Detail
    {
        /** @brief Where the pose pipeline is in a scene: which step runs now, if any. */
        enum class PipelineStep : std::uint8_t
        {
            None,
            Reconcile,
            Animate,
            Prepare,
            Modify,
            Finalise,
            Attach,
            Late,
        };

        /** @brief One skeleton instance: the pose arrays of one skeleton entity, indexed by bone. */
        struct SkeletonInstance
        {
            Entity entity;
            AssetHandle<Skeleton> skeleton;
            std::uint32_t revision = 0;
            PoseTiming timing = PoseTiming::Frame;
            bool ready = false;
            // Unavailability is logged once, until the instance is next Ready.
            bool reportedUnavailable = false;

            // Copied from the asset when the instance is built, so a Replace before the next reconcile cannot leave
            // the arrays and the bones disagreeing.
            std::vector<std::int32_t> parents;
            std::vector<Math::Mat4> inverseBinds;
            std::unordered_map<std::string, std::int32_t> bonesByName;

            std::vector<Transform> base;
            std::vector<Transform> final;
            std::vector<Math::Mat4> model;
            // A model matrix is stale once a write moves its bone or an ancestor; the set of stale bones is closed
            // under descendants.
            std::vector<std::uint8_t> modelValid;
            std::vector<Math::Mat4> baseModel;
            bool baseModelValid = false;
            std::vector<Math::Mat4> palette;
            std::uint64_t paletteGeneration = 0;
            std::uint64_t generation = 0;
            // The generation the instance was last rebuilt at, so bone attachments resolved against an older build
            // resolve again.
            std::uint64_t built = 0;

            // The base pose changed since the last finalise.
            bool poseDirty = false;
            // Prepared this frame: final copied from base, to be finalised.
            bool prepared = false;
            // A modifier opened an edit this frame, or the last: the final pose differs from the base pose until the
            // next prepare.
            bool modifiedThisFrame = false;
            bool modifiedLastFrame = false;
            // The inverse of the skeleton entity's world matrix, read at the first edit of the frame's Modify.
            std::optional<Math::Mat4> worldInverse;
            std::uint64_t worldInverseFrame = 0;

            // The pose override (ADR-0033, ADR-0038): each bone's persistent entry, if any, and its override weight.
            // Both are dropped when the instance is rebuilt.
            std::vector<std::optional<Target>> overrides;
            std::vector<float> overrideWeights;
            std::size_t overrideCount = 0;
        };

        /**
         * @brief The private scene resource holding a scene's skeleton instances (ADR-0031), and where that scene's
         * pose pipeline is. One pose generation counter serves the whole resource, so an instance rebuilt under a
         * recycled entity never repeats a generation.
         */
        class SkeletonPoses
        {
          public:
            explicit SkeletonPoses(ILogger& logger) noexcept : m_logger(&logger)
            {
            }

            /** @brief The entity's instance, Ready or not; null when it has none. */
            [[nodiscard]] SkeletonInstance* Find(Entity entity);

            /** @brief The entity's instance when it is Ready; null otherwise. */
            [[nodiscard]] SkeletonInstance* FindReady(Entity entity);

            /** @brief Makes, rebuilds or drops the entity's instance against its component and the store. */
            PoseState Reconcile(const AssetStore& store, Entity entity);

            /** @brief Reconciles every `SkeletonPose` in the scene, and drops instances whose entity or component went.
             */
            void ReconcileAll(const AssetStore& store, Scene& scene);

            /** @brief Every Ready instance's entity, in entity id order. */
            [[nodiscard]] std::vector<Entity> ReadyEntities(std::optional<PoseTiming> timing = std::nullopt) const;

            /** @brief Calls `visit(instance)` for every instance. */
            template <typename Visit>
            void ForEach(Visit&& visit)
            {
                for (auto& [id, instance] : m_instances)
                {
                    visit(instance);
                }
            }

            /** @brief Final ← base, every model matrix stale. */
            static void Prepare(SkeletonInstance& instance);

            /** @brief Computes every stale model matrix and gives the instance the next pose generation. */
            void Finalise(SkeletonInstance& instance);

            /** @brief The bone's final-pose model matrix, recomputing it and its stale ancestors first. */
            static const Math::Mat4& ModelOf(SkeletonInstance& instance, std::int32_t bone);

            /** @brief Every model matrix, none stale. */
            static std::span<const Math::Mat4> AllModels(SkeletonInstance& instance);

            /** @brief Every base-pose model matrix, computed on first read after a base-pose change. */
            static std::span<const Math::Mat4> AllBaseModels(SkeletonInstance& instance);

            /** @brief The palette for the instance's current generation, built if it has none. */
            static std::span<const Math::Mat4> PaletteOf(SkeletonInstance& instance);

            /** @brief Marks `bone` and every bone below it stale. */
            static void Invalidate(SkeletonInstance& instance, std::int32_t bone);

            /** @brief Logs `message` as a warning once for the call site `site`. */
            void WarnOncePerSite(std::source_location site, std::string_view message);

            /**
             * @brief Logs `message` as a warning for the entity and bone, at most once every few seconds of frames, so
             * a transform held degenerate does not flood the log.
             */
            void WarnRateLimited(Entity entity, std::int32_t bone, std::string_view message);

            [[nodiscard]] ILogger& Logger() const noexcept
            {
                return *m_logger;
            }

            /** @brief The pipeline step running in this scene now. */
            PipelineStep step = PipelineStep::None;
            /** @brief The running `Animate` pass's timing. */
            PoseTiming passTiming = PoseTiming::Frame;
            /** @brief Counts modifier calls, so a pose edit kept past its call is empty. */
            std::uint64_t modifierCall = 0;
            /** @brief The frame the pipeline last ran in, for rate-limited warnings. */
            std::uint64_t frame = 0;
            /** @brief The scene's bone attachments: their resolutions, states and order. */
            Attachments attachments;

          private:
            void Rebuild(SkeletonInstance& instance, const SkeletonData& data, AssetHandle<Skeleton> handle,
                         std::uint32_t revision);
            void MakeUnavailable(SkeletonInstance& instance, AssetHandle<Skeleton> handle);

            ILogger* m_logger;
            std::unordered_map<std::uint32_t, SkeletonInstance> m_instances;
            std::uint64_t m_nextGeneration = 1;
            std::unordered_set<std::string> m_warnedSites;
            std::unordered_map<std::uint64_t, std::uint64_t> m_lastWarnedFrame;
        };

        /** @brief Whether `matrix` has a usable inverse: finite, and its 3×3 not near-singular for its size. */
        [[nodiscard]] bool Invertible(const Math::Mat4& matrix);

        /** @brief The skeleton entity's name for log messages: its `Name`, or its id. */
        [[nodiscard]] std::string Describe(Entity entity);
    } // namespace Detail
} // namespace Fenrir
