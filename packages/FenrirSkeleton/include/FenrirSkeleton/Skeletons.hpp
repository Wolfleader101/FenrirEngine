#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirAssets/SkinningSource.hpp"
#include "FenrirAssets/SpawnNodes.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirSkeleton/BoneAttachment.hpp"
#include "FenrirSkeleton/PoseStages.hpp"
#include "FenrirSkeleton/SkeletonPose.hpp"

namespace Fenrir
{
    class App;
    class Scene;

    namespace Detail
    {
        struct SkeletonsState;
    } // namespace Detail

    /**
     * @brief The owner and facade of FenrirSkeleton (ADR-0028, ADR-0031, ADR-0033, ADR-0041): skeleton instances and
     * their reads, the pose pipeline and its stages, and spawning. Move-only. Destroy it before the App, and keep it
     * alive while the App runs.
     *
     * **Instances.** A skeleton entity is an entity with a `SkeletonPose`. Its instance (base and final locals, model
     * matrices, the cached palette) lives in a private scene resource. Each pose pipeline reconciles it: an instance is
     * made for a new component, rebuilt at the rest pose when its handle or the asset's revision changes, and dropped
     * when the entity or component is gone. `Ensure` does that at once for one entity.
     *
     * **Reads** are static, so pose stage work and other packages need no owner. Each takes the scene and the skeleton
     * entity. On an entity that is not a `Ready` instance, spans are empty and the rest `nullopt`, `NoBone` or 0. A
     * span is valid until the end of the current system or the next change to the scene's instances (reconcile,
     * `Ensure`, a rebuild, a sweep), whichever comes first.
     *
     * **Three kinds of change stay apart.** A pose change (`EditBasePose`, a modifier) dirties the model matrices and
     * the palette, and advances the pose generation when the pipeline finalises it. A move of the skeleton entity
     * changes only world reads. A change of asset or revision rebuilds the instance.
     *
     * **The pose pipeline** runs once a frame in one sequential `PostUpdate` system, on the active scene only:
     * reconcile, `Animate` (base-pose writers), prepare (final pose from base pose), `Modify` (pose modifiers),
     * finalise (model matrices left, parents first), attach (bone attachments placed, in dependency order) and `Late`
     * (read-only for poses). Inactive scenes pause, and resume without catching up. Work added to a stage runs by its
     * `int` order, ties in the order it was added; nothing is promised against other `PostUpdate` systems. An instance
     * whose pose is clean and that no modifier edited this frame or the last skips prepare and finalise.
     *
     * **Tick-posed skeletons** (`PoseTiming::Tick`, ADR-0040) evaluate their base pose in a tick pass, in `PreTick` of
     * each fixed tick, on the active scene, while it has one: reconcile, `Animate` over the tick-posed instances,
     * stepping by `Time::tickRate`, then their base model matrices. That is the **simulation pose**, with no
     * modifiers; the tick before it is kept beside it. A frame with no tick evaluates nothing. In the frame pass a
     * tick-posed instance skips `Animate`: its base pose is the last two ticks' blended at `Time::accumulator /
     * Time::tickRate` (translation and scale lerp, rotation slerps), the fraction physics interpolates bodies by, and
     * the rest of the frame pass gives the **render pose**, which the palette and attachments use. Modifiers are
     * render-only. A base-pose write made before a `PreTick` is seen by that tick, one made in `Tick` by the next.
     * `PoseKind::Simulation` reads the simulation pose; on a frame-posed skeleton both kinds read the render pose.
     */
    class Skeletons
    {
      public:
        /**
         * @brief Turns FenrirSkeleton on for `app`: registers its skinning source as an app resource (a
         * `SkinningSourceRegistration`) and adds the pose pipeline's systems: the frame pass in `PostUpdate` and the
         * tick pass in `PreTick`. It fails, with one logged diagnostic and an owner whose `IsValid()` is false that
         * has added nothing, without FenrirAssets installed, or when a skinning source is already registered (a second
         * install).
         */
        [[nodiscard]] static Skeletons Install(App& app);

        Skeletons(Skeletons&& other) noexcept;
        Skeletons& operator=(Skeletons&& other) noexcept;
        Skeletons(const Skeletons&) = delete;
        Skeletons& operator=(const Skeletons&) = delete;
        /** @brief Removes the skinning source and every scene's instances; the pipeline's systems then do nothing. */
        ~Skeletons();

        /** @brief Whether this owner holds an install: false when Install failed, and once moved from. */
        [[nodiscard]] bool IsValid() const noexcept;

        /** @name Pose stages
         * Work added to `Animate`, `Modify` or `Late` at an `int` order. Packages use order constants
         * (`ModifierOrder`) rather than relying on ties. Each call returns a handle that `Remove` takes; on an owner
         * that is not valid it adds nothing and returns the empty handle.
         * @{
         */

        /** @brief Adds a base-pose writer to `Animate`, which may write only the instances its context lists. */
        PoseStageHandle AddAnimate(std::string name, int order, std::function<void(AnimateContext&)> work);

        /** @brief Adds a pose modifier to `Modify`. It runs every frame and writes only through pose edits. */
        PoseStageHandle AddModifier(std::string name, int order, std::function<void(ModifierContext&)> work);

        /** @brief Adds work to `Late`, which reads this frame's final poses and may write ordinary ECS state only. */
        PoseStageHandle AddLate(std::string name, int order, std::function<void(LateContext&)> work);

        /**
         * @brief Removes the work `handle` names. Refused, with a logged error, while the pose pipeline runs; false
         * then, and for a handle that names nothing.
         */
        bool Remove(PoseStageHandle handle);
        /** @} */

        /**
         * @brief Reconciles one entity at once, as the pipeline would: makes, rebuilds or drops its instance. Returns
         * its state after.
         */
        PoseState Ensure(Scene& scene, Entity skeleton) const;

        /**
         * @brief Gives the nodes `SpawnNodes` spawned for `model` their skeletons: a `SkeletonPose` on each skeleton's
         * owner (an ordinary node or a synthetic owner) whose skeleton loaded, each made `Ready` with `Ensure`. An
         * owner that already has a `SkeletonPose` keeps it, with a logged warning. Then every ordinary node whose
         * parent node is a bone of a skeleton that loaded gets a `BoneAttachment` to that bone, its rest transform
         * relative to the bone as the offset: a named attachment point is such a node, already attached (ADR-0037).
         */
        void SpawnSkeletons(Scene& scene, AssetHandle<Model> model, const SpawnedNodes& spawned) const;

        /** @brief Spawns `model` with its skeletons, for apps with no renderer: `SpawnNodes`, then `SpawnSkeletons`. */
        SpawnedNodes SpawnModel(Scene& scene, AssetHandle<Model> model, Entity parent = {}) const;

        /** @name Reads
         * @{
         */

        /** @brief Whether the entity's pose can be read: `Ready`, `Unavailable` (an empty or stale handle) or
         * `Missing`.
         */
        [[nodiscard]] static PoseState GetPoseState(Scene& scene, Entity skeleton);

        /** @brief The bone whose engine name is exactly `name`, or `NoBone`. */
        [[nodiscard]] static std::int32_t FindBone(Scene& scene, Entity skeleton, std::string_view name);

        /** @brief The final pose's local transforms, one per bone. */
        [[nodiscard]] static std::span<const Transform> GetLocals(Scene& scene, Entity skeleton);

        /** @brief The base pose's local transforms, one per bone: the simulation pose's, for `Simulation` on a
         * tick-posed skeleton.
         */
        [[nodiscard]] static std::span<const Transform> GetBaseLocals(Scene& scene, Entity skeleton,
                                                                      PoseKind kind = PoseKind::Render);

        /** @brief The final pose's model-space matrices, one per bone, relative to the skeleton entity. */
        [[nodiscard]] static std::span<const Math::Mat4> GetModelMatrices(Scene& scene, Entity skeleton);

        /** @brief The bone's local transform in the final pose. */
        [[nodiscard]] static std::optional<Transform> GetLocal(Scene& scene, Entity skeleton, std::int32_t bone);

        /** @brief The bone's model-space matrix in the final pose. */
        [[nodiscard]] static std::optional<Math::Mat4> GetModel(Scene& scene, Entity skeleton, std::int32_t bone);

        /** @brief The bone's local transform in the base pose. */
        [[nodiscard]] static std::optional<Transform> GetBaseLocal(Scene& scene, Entity skeleton, std::int32_t bone,
                                                                   PoseKind kind = PoseKind::Render);

        /** @brief The bone's model-space matrix in the base pose, computed on first read and cached until it changes.
         */
        [[nodiscard]] static std::optional<Math::Mat4> GetBaseModel(Scene& scene, Entity skeleton, std::int32_t bone,
                                                                    PoseKind kind = PoseKind::Render);

        /**
         * @brief The bone's world matrix: `GetWorldMatrix(skeleton) × M_model[bone]`, the one defined way to combine
         * the skeleton entity's space with its pose. `M_model` is the final pose's for `Render`, and for `Simulation`
         * on a tick-posed skeleton the simulation pose's; the skeleton entity's world matrix is read now. `nullopt`
         * when the entity is not `Ready` or the bone is out of range.
         */
        [[nodiscard]] static std::optional<Math::Mat4> GetBoneWorldMatrix(Scene& scene, Entity skeleton,
                                                                          std::int32_t bone,
                                                                          PoseKind kind = PoseKind::Render);

        /**
         * @brief The pose generation (ADR-0035): advanced when finalisation changes the final pose and when the
         * instance is rebuilt, drawn from one counter for the scene so a recycled entity never repeats one, and never
         * advanced by moving the skeleton entity. 0 when not `Ready`.
         */
        [[nodiscard]] static std::uint64_t GetPoseGeneration(Scene& scene, Entity skeleton);

        /**
         * @brief The skinning palette, one matrix per bone (model × inverse bind), built on the first read for the
         * current pose generation and cached with it. Main thread only. A skeleton nothing reads it for never builds
         * one.
         */
        [[nodiscard]] static std::span<const Math::Mat4> GetPalette(Scene& scene, Entity skeleton);
        /** @} */

        /**
         * @brief The base pose's local transforms to write, marking the instance pose-dirty whether or not a value
         * changes. Empty when the entity is not `Ready`.
         *
         * Before the pipeline, the write is seen by this frame's pose; after it, by the next frame's. During `Animate`
         * it works only on instances of the running pass's timing; during `Modify` and `Late`, which may not write the
         * base pose, not at all. A refused call writes nothing, returns an empty span and logs once per call site.
         *
         * On a tick-posed skeleton the span is the simulation pose, the base pose the next tick evaluates from: a
         * write before a `PreTick` is seen by that tick, one in `Tick` by the next, and the frame draws it through the
         * blend.
         */
        [[nodiscard]] static std::span<Transform> EditBasePose(
            Scene& scene, Entity skeleton, std::source_location site = std::source_location::current());

        /** @name Bone attachments (ADR-0037)
         * A `BoneAttachment` places its entity in the pose pipeline's Attach step, after finalise and before `Late`,
         * every frame, in a cached order of their dependencies (an attachment whose skeleton entity or parent, or an
         * ancestor of either, is attached comes after that attachment; ties by entity index). Under the skeleton entity
         * the entity's `Transform` is `M_model[bone] × offset`; under any other parent, the bone's world matrix ×
         * `offset` taken into the parent's space, with shear dropped. An attachment that cannot be placed keeps its
         * last `Transform`, reports why through `GetAttachmentState`, logs a warning once per cause, and recovers by
         * itself.
         * @{
         */

        /** @brief Whether the last Attach placed the entity, and why not. */
        [[nodiscard]] static AttachmentState GetAttachmentState(Scene& scene, Entity entity);

        /**
         * @brief Detaches the entity from its bone. `KeepLocal` removes the `BoneAttachment` only. `KeepWorld` also
         * reparents the entity under `newParent`, or to the top of the scene when it is null, with the local
         * `Transform` that keeps its world transform as it is in the scene now (where the last Attach placed it).
         * `KeepWorld` is transactional: it checks the new parent and the new transform first, and on any failure
         * changes nothing. A structural change: refused inside the pose pipeline.
         */
        static DetachResult Detach(Scene& scene, Entity entity, DetachMode mode, Entity newParent = {});
        /** @} */

        /** @name The pose override (ADR-0033, ADR-0038)
         * `PoseOverride`, FenrirSkeleton's own modifier at `ModifierOrder::Override` (1000): the first modifier, not
         * the contract, so a modifier at a higher order (or added later at 1000) sees its results. Each instance holds
         * a persistent entry per bone, `{ bone, Target }`, written each frame in bone order (parents first, bones with
         * no entry skipped), so a `World` target goes through its parent's current model matrix, after any override of
         * an ancestor; a bone with no entry keeps its animated local, and the bones below an overridden one follow it.
         *
         * Each bone also has an **override weight**, default 1, gameplay's: an entry is written at `clamp(Target.weight
         * × overrideWeight, 0, 1)`, and skipped at 0. Entries are the glue's, refreshed each frame; weights survive
         * every entry update and clear. FenrirSkeleton has no fades: game code ramps the weights. The last
         * `SetOverride` on a bone wins, whoever made it. Entries and weights are both dropped when the instance is
         * destroyed or rebuilt (a handle or asset revision change).
         *
         * Each call works on a `Ready` instance, from the main thread (glue writes its targets in `Update`, before the
         * pose pipeline); on anything else it does nothing and returns false (or 1 for a weight). Base-pose reads
         * (`GetBaseLocal`, `GetBaseModel`) never see overrides.
         * @{
         */

        /** @brief Sets the bone's entry, replacing any. */
        static bool SetOverride(Scene& scene, Entity skeleton, std::int32_t bone, const Target& target);

        /** @brief The bone's entry, if it has one. */
        [[nodiscard]] static std::optional<Target> GetOverride(Scene& scene, Entity skeleton, std::int32_t bone);

        /** @brief Removes the bone's entry; its override weight stays. False when it had none. */
        static bool ClearOverride(Scene& scene, Entity skeleton, std::int32_t bone);

        /** @brief Removes every entry of the instance; override weights stay. */
        static bool ClearOverrides(Scene& scene, Entity skeleton);

        /**
         * @brief Sets the bone's override weight, or with `BoneSubtree` the bone's and every descendant's, each to
         * `weight` clamped to [0, 1]: a later assignment replaces earlier ones bone by bone, never multiplying.
         */
        static bool SetOverrideWeight(Scene& scene, Entity skeleton, std::int32_t bone, float weight,
                                      OverrideScope scope = OverrideScope::Bone);

        /** @brief The bone's override weight: 1 unless set, and 1 for a bone or entity that has none. */
        [[nodiscard]] static float GetOverrideWeight(Scene& scene, Entity skeleton, std::int32_t bone);

        /** @brief Sets the bone's override weight (or its subtree's) back to 1. */
        static bool ClearOverrideWeight(Scene& scene, Entity skeleton, std::int32_t bone,
                                        OverrideScope scope = OverrideScope::Bone);

        /** @brief The bone and every bone below it, in index order (each after its parent); empty when out of range.
         */
        [[nodiscard]] static std::vector<std::int32_t> GetSubtree(Scene& scene, Entity skeleton, std::int32_t bone);
        /** @} */

      private:
        explicit Skeletons(std::shared_ptr<Detail::SkeletonsState> state) noexcept;

        void Shutdown() noexcept;

        // Shared with the installed system, which holds it weakly: once the owner is gone it does nothing.
        std::shared_ptr<Detail::SkeletonsState> m_state;
    };
} // namespace Fenrir
