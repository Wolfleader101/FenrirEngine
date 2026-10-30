#include "FenrirSkeletonScripting/SkeletonScripting.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/Model.hpp"
#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirAssets/SkinningSource.hpp"
#include "FenrirAssets/SpawnNodes.hpp"
#include "FenrirAssetsScripting/AssetResolve.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirScripting/NativeGuard.hpp"
#include "FenrirScripting/NativeMarshalling.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirSkeleton/BoneAttachment.hpp"
#include "FenrirSkeleton/PoseStages.hpp"
#include "FenrirSkeleton/SkeletonPose.hpp"
#include "FenrirSkeleton/Skeletons.hpp"
#include "Generated/FenrirSkeletonNatives.hpp"
#include "SkeletonScriptingState.hpp"

// Everything C# sees of skeletons is generated (api/FenrirSkeleton.binding.toml): the components' and shared struct's
// mappings, checked against the Fenrir.Skeleton contract whenever a script assembly loads, and from the native
// functions below (described into api/FenrirSkeleton.natives.json) their C# declarations, SkeletonNative, and this
// registration of them all. The pose stages are placed by hand, below.
namespace Fenrir::Generated
{
    void RegisterSkeleton(ScriptingExtension& extension);
} // namespace Fenrir::Generated

namespace Fenrir
{
    namespace
    {
        // Writes `value` through `out` if there is one, and says whether there was.
        template <typename V>
        bool Write(const std::optional<V>& value, V* out)
        {
            if (value.has_value())
            {
                *out = *value;
                return true;
            }
            return false;
        }

        // Copies as much of `from` as `count` elements at `out` hold, and returns how many it copied.
        template <typename T>
        std::int32_t CopyInto(std::span<const T> from, T* out, std::int32_t count)
        {
            const std::span<T> room = SpanOf(out, count);
            const std::size_t copied = std::min(room.size(), from.size());
            std::copy_n(from.begin(), copied, room.begin());
            return static_cast<std::int32_t>(copied);
        }

        // A per-bone read of a Ready skeleton entity: `read` with its handle, when the bone is in range.
        template <typename V, typename Read>
        bool ReadBone(const CallContext& context, SkeletonScripting& skeletons, const Entity& entity, std::int32_t bone,
                      V* out, Read&& read)
        {
            const std::optional<Entity> ready = skeletons.ReadyOf(context, entity);
            if (!ready)
            {
                return false;
            }
            const std::size_t count = Skeletons::GetLocals(context.GetApp().GetActiveScene(), *ready).size();
            return skeletons.InRange(context, bone, count) && Write(read(*ready), out);
        }

        // A query on the skeleton asset `skeleton` names: `query` with its data, or `otherwise` with a warning once if
        // the app has no store or the handle is stale.
        template <typename Result, typename Query>
        Result QuerySkeleton(const CallContext& context, SkeletonScripting& skeletons,
                             const AssetHandle<Skeleton>& skeleton, Result otherwise, Query&& query)
        {
            const NativeGuard guard(context, skeletons.warnings);
            AssetStore* store = Resolve<Skeleton>(guard, skeleton);
            const SkeletonData* data = store == nullptr ? nullptr : store->TryGet(skeleton);
            return data == nullptr ? otherwise : query(*data);
        }

        // The current pose edit for a call through `token` on `entity`: the running Modify group's, opened again (it
        // is valid for this modifier call only). Empty, with a warning once, when the token is stale; empty when the
        // entity is no Ready skeleton.
        PoseEdit EditFor(SkeletonScripting& skeletons, const CallContext& context, std::uint64_t token,
                         const Entity& entity)
        {
            const NativeGuard guard(context, skeletons.warnings);
            if (skeletons.modifier == nullptr || token != skeletons.editToken)
            {
                guard.Warn(std::string(SkeletonScripting::StaleEdit));
                return {};
            }
            const std::optional<Entity> ready = skeletons.ReadyOf(context, entity);
            return ready ? skeletons.modifier->Edit(*ready) : PoseEdit{};
        }

        // Records `source`, an entity of a scratch scene, and its children, depth first, as new entities of the active
        // scene with its Name and Transform, `source` a child of `parent` if there is one, noting each new entity's id
        // under its scratch id in `made`.
        std::uint32_t Record(const ChangeBuffer& changes, const Entity& source, std::optional<std::uint32_t> parent,
                             std::unordered_map<std::uint32_t, std::uint32_t>& made)
        {
            const std::uint32_t entity = changes.CreateEntity();
            made[source.GetId()] = entity;
            (void)changes.AddComponent<Name>(entity, source.GetComponent<Name>());
            (void)changes.AddComponent<Transform>(entity, source.GetComponent<Transform>());
            if (parent)
            {
                (void)changes.AddChild(*parent, entity);
            }
            source.ForEachChild([&](const Entity& child) { (void)Record(changes, child, entity, made); });
            return entity;
        }

        // The entity `id` names in the active scene now, or none if it is not alive.
        Entity HandleIn(App& app, std::uint32_t id)
        {
            EntityList& entities = app.GetActiveScene().GetEntityList();
            return entities.HasEntity(id) ? entities.GetEntity(id) : Entity{};
        }

        std::string_view ResultName(DetachResult result)
        {
            switch (result)
            {
            case DetachResult::Detached:
                return "detached";
            case DetachResult::NotAttached:
                return "it was no longer attached";
            case DetachResult::InsidePipeline:
                return "it was inside the pose pipeline";
            case DetachResult::InvalidParent:
                return "the new parent was dead, the entity itself or one of its descendants";
            case DetachResult::ParentNotInvertible:
                return "the new parent's world matrix had no inverse";
            case DetachResult::NotRepresentable:
                return "its world transform could not be held under the new parent";
            }
            return "an unknown result";
        }
    } // namespace

    // The native functions behind Fenrir.Skeleton's Skeletons, SkeletonData and PoseStages, each named as C# calls it.
    // tools/describe-api reads them from this file, in this order, and Iris.Bindings generates their C# declarations
    // (Fenrir.Skeleton.SkeletonNative) and their registration from that. Each trusts the declaration (ADR-0045): an
    // `in` or `out` pointer is never null, and the registration zeroes each single output before the native runs. On an
    // entity that is not alive, not a Ready skeleton entity, or a bone out of range each does nothing and returns
    // false, 0, -1 or nothing, warning once per call site in a debug build under its own name; FenrirSkeleton not
    // installed reads as no skeleton being Ready. Not in an anonymous namespace: the generated natives header declares
    // them.
    namespace SkeletonNatives
    {
        /**
         * @brief Whether the entity's pose can be read: Ready, Unavailable (an empty or stale skeleton handle) or
         * Missing (dead, or no SkeletonPose).
         * @facade Skeletons.GetPoseState
         */
        PoseState GetPoseState(const CallContext& context, SkeletonScripting& skeletons, const Entity* entity)
        {
            const NativeGuard guard(context, skeletons.warnings);
            const std::optional<Entity> alive = guard.Alive(*entity);
            return alive ? Skeletons::GetPoseState(context.GetApp().GetActiveScene(), *alive) : PoseState::Missing;
        }

        /**
         * @brief The pose generation (ADR-0035): a number that moves on whenever the final pose changes or the instance
         * is rebuilt, never repeating in the scene, so a cache can key on it. 0 when not Ready.
         * @facade Skeletons.GetPoseGeneration
         */
        std::uint64_t GetPoseGeneration(const CallContext& context, SkeletonScripting& skeletons, const Entity* entity)
        {
            const std::optional<Entity> ready = skeletons.ReadyOf(context, *entity);
            return ready ? Skeletons::GetPoseGeneration(context.GetApp().GetActiveScene(), *ready) : 0;
        }

        /**
         * @brief The bone of the skeleton entity whose engine name is exactly `name`.
         * @return The bone, or -1 when none has that name or the entity is not Ready.
         * @facade Skeletons.FindBone
         */
        std::int32_t FindBone(const CallContext& context, SkeletonScripting& skeletons, const Entity* entity,
                              std::string_view name)
        {
            const std::optional<Entity> ready = skeletons.ReadyOf(context, *entity);
            return ready ? Skeletons::FindBone(context.GetApp().GetActiveScene(), *ready, name) : NoBone;
        }

        /**
         * @brief The bone's local transform in the final pose: this frame's after Modify, the last frame's before.
         * @param[in] entity The skeleton entity.
         * @param[out] local The transform; the default when it reads nothing.
         * @return Whether it read: false when the entity is not Ready or the bone is out of range.
         * @facade Skeletons.GetLocal
         */
        bool GetLocal(const CallContext& context, SkeletonScripting& skeletons, const Entity* entity, std::int32_t bone,
                      Transform* local)
        {
            return ReadBone(context, skeletons, *entity, bone, local, [&](const Entity& ready) {
                return Skeletons::GetLocal(context.GetApp().GetActiveScene(), ready, bone);
            });
        }

        /**
         * @brief The bone's local transform in the base pose, the render pose's or the simulation pose's (`kind`).
         * @param[in] entity The skeleton entity.
         * @param[out] local The transform; the default when it reads nothing.
         * @return Whether it read: false when the entity is not Ready or the bone is out of range.
         * @facade Skeletons.GetBaseLocal
         */
        bool GetBaseLocal(const CallContext& context, SkeletonScripting& skeletons, const Entity* entity,
                          std::int32_t bone, PoseKind kind, Transform* local)
        {
            return ReadBone(context, skeletons, *entity, bone, local, [&](const Entity& ready) {
                return Skeletons::GetBaseLocal(context.GetApp().GetActiveScene(), ready, bone, kind);
            });
        }

        /**
         * @brief The bone's model-space matrix in the final pose, relative to the skeleton entity.
         * @param[in] entity The skeleton entity.
         * @param[out] model The matrix; zero when it reads nothing.
         * @return Whether it read: false when the entity is not Ready or the bone is out of range.
         * @facade Skeletons.GetModel
         */
        bool GetModel(const CallContext& context, SkeletonScripting& skeletons, const Entity* entity, std::int32_t bone,
                      Math::Mat4* model)
        {
            return ReadBone(context, skeletons, *entity, bone, model, [&](const Entity& ready) {
                return Skeletons::GetModel(context.GetApp().GetActiveScene(), ready, bone);
            });
        }

        /**
         * @brief The bone's model-space matrix in the base pose (`kind`'s), relative to the skeleton entity.
         * @param[in] entity The skeleton entity.
         * @param[out] model The matrix; zero when it reads nothing.
         * @return Whether it read: false when the entity is not Ready or the bone is out of range.
         * @facade Skeletons.GetBaseModel
         */
        bool GetBaseModel(const CallContext& context, SkeletonScripting& skeletons, const Entity* entity,
                          std::int32_t bone, PoseKind kind, Math::Mat4* model)
        {
            return ReadBone(context, skeletons, *entity, bone, model, [&](const Entity& ready) {
                return Skeletons::GetBaseModel(context.GetApp().GetActiveScene(), ready, bone, kind);
            });
        }

        /**
         * @brief The bone's world matrix: the skeleton entity's world matrix now times the bone's model matrix, the
         * final pose's for Render and, on a tick-posed skeleton, the simulation pose's for Simulation.
         * @param[in] entity The skeleton entity.
         * @param[out] world The matrix; zero when it reads nothing.
         * @return Whether it read: false when the entity is not Ready or the bone is out of range.
         * @facade Skeletons.GetBoneWorldMatrix
         */
        bool GetBoneWorldMatrix(const CallContext& context, SkeletonScripting& skeletons, const Entity* entity,
                                std::int32_t bone, PoseKind kind, Math::Mat4* world)
        {
            return ReadBone(context, skeletons, *entity, bone, world, [&](const Entity& ready) {
                return Skeletons::GetBoneWorldMatrix(context.GetApp().GetActiveScene(), ready, bone, kind);
            });
        }

        /**
         * @brief Copies the final pose's local transforms, one per bone in bone order, into `locals`.
         * @return How many bones it wrote: the fewer of the span's length and the bone count; 0 when not Ready.
         * @facade Skeletons.ReadLocals
         */
        std::int32_t ReadLocals(const CallContext& context, SkeletonScripting& skeletons, const Entity* entity,
                                Transform* locals, std::int32_t count)
        {
            const std::optional<Entity> ready = skeletons.ReadyOf(context, *entity);
            return ready ? CopyInto(Skeletons::GetLocals(context.GetApp().GetActiveScene(), *ready), locals, count) : 0;
        }

        /**
         * @brief Copies the base pose's local transforms (`kind`'s), one per bone in bone order, into `locals`.
         * @return How many bones it wrote: the fewer of the span's length and the bone count; 0 when not Ready.
         * @facade Skeletons.ReadBaseLocals
         */
        std::int32_t ReadBaseLocals(const CallContext& context, SkeletonScripting& skeletons, const Entity* entity,
                                    Transform* locals, std::int32_t count, PoseKind kind)
        {
            const std::optional<Entity> ready = skeletons.ReadyOf(context, *entity);
            return ready ? CopyInto(Skeletons::GetBaseLocals(context.GetApp().GetActiveScene(), *ready, kind), locals,
                                    count)
                         : 0;
        }

        /**
         * @brief Copies the final pose's model-space matrices, one per bone in bone order, into `models`.
         * @return How many bones it wrote: the fewer of the span's length and the bone count; 0 when not Ready.
         * @facade Skeletons.ReadModel
         */
        std::int32_t ReadModel(const CallContext& context, SkeletonScripting& skeletons, const Entity* entity,
                               Math::Mat4* models, std::int32_t count)
        {
            const std::optional<Entity> ready = skeletons.ReadyOf(context, *entity);
            return ready
                       ? CopyInto(Skeletons::GetModelMatrices(context.GetApp().GetActiveScene(), *ready), models, count)
                       : 0;
        }

        /**
         * @brief Writes the bone's local transform in the base pose, marking the pose dirty (C++'s EditBasePose): seen
         * by this frame's pose before the pipeline or in Animate, by the next frame's after it. Refused, writing
         * nothing, in Modify and Late, and during Animate on a skeleton of the other pass's timing.
         * @param[in] entity The skeleton entity.
         * @return Whether it wrote.
         * @facade Skeletons.SetBaseLocal
         */
        bool SetBaseLocal(const CallContext& context, SkeletonScripting& skeletons, const Entity* entity,
                          std::int32_t bone, const Transform* local)
        {
            const std::optional<Entity> ready = skeletons.ReadyOf(context, *entity);
            if (!ready)
            {
                return false;
            }
            const std::span<Transform> base = Skeletons::EditBasePose(context.GetApp().GetActiveScene(), *ready);
            if (base.empty())
            {
                const NativeGuard guard(context, skeletons.warnings);
                guard.Warn(std::string(SkeletonScripting::BaseWriteRefused));
                return false;
            }
            if (!skeletons.InRange(context, bone, base.size()))
            {
                return false;
            }
            base[static_cast<std::size_t>(bone)] = *local;
            return true;
        }

        /**
         * @brief Writes the base pose's local transforms from `pose`, one per bone in bone order, as SetBaseLocal does
         * each. A span shorter than the bone count leaves the bones after it as they are; a longer one's extra entries
         * are ignored.
         * @return How many bones it wrote; 0 when refused or not Ready.
         * @facade Skeletons.SetBasePose
         */
        std::int32_t SetBasePose(const CallContext& context, SkeletonScripting& skeletons, const Entity* entity,
                                 const Transform* pose, std::int32_t count)
        {
            const std::optional<Entity> ready = skeletons.ReadyOf(context, *entity);
            if (!ready)
            {
                return 0;
            }
            const std::span<Transform> base = Skeletons::EditBasePose(context.GetApp().GetActiveScene(), *ready);
            if (base.empty())
            {
                const NativeGuard guard(context, skeletons.warnings);
                guard.Warn(std::string(SkeletonScripting::BaseWriteRefused));
                return 0;
            }
            const std::span<const Transform> from = SpanOf(pose, count);
            const std::size_t written = std::min(base.size(), from.size());
            std::copy_n(from.begin(), written, base.begin());
            return static_cast<std::int32_t>(written);
        }

        /**
         * @brief The running pass's timing, inside a Fenrir.Skeleton.Animate stage system: Frame in the frame pass,
         * Tick in a tick pass. Frame anywhere else.
         */
        PoseTiming GetTiming(const CallContext& context, SkeletonScripting& skeletons)
        {
            if (skeletons.animate == nullptr)
            {
                const NativeGuard guard(context, skeletons.warnings);
                guard.Warn(std::string(SkeletonScripting::OutsideAnimate));
                return PoseTiming::Frame;
            }
            return skeletons.animate->Timing();
        }

        /**
         * @brief The running pass's step in seconds, inside a Fenrir.Skeleton.Animate stage system: the frame's delta
         * in the frame pass, the tick rate in a tick pass. 0 anywhere else.
         */
        double GetStep(const CallContext& context, SkeletonScripting& skeletons)
        {
            if (skeletons.animate == nullptr)
            {
                const NativeGuard guard(context, skeletons.warnings);
                guard.Warn(std::string(SkeletonScripting::OutsideAnimate));
                return 0.0;
            }
            return skeletons.animate->Step();
        }

        /**
         * @brief Opens a pose edit on the skeleton entity for the running Fenrir.Skeleton.Modify stage system (C#'s
         * Skeletons.Edit): its token, valid until the system's group returns.
         * @param[out] token The edit's token; 0 outside Modify, when the edit writes nothing.
         * @return Whether the edit reaches a Ready instance: false outside Modify, or on an entity that is not Ready.
         */
        bool EditOpen(const CallContext& context, SkeletonScripting& skeletons, const Entity* entity,
                      std::uint64_t* token)
        {
            if (skeletons.modifier == nullptr)
            {
                const NativeGuard guard(context, skeletons.warnings);
                guard.Warn(std::string(SkeletonScripting::OutsideModify));
                return false;
            }
            *token = skeletons.editToken;
            const std::optional<Entity> ready = skeletons.ReadyOf(context, *entity);
            return ready && !skeletons.modifier->Edit(*ready).IsEmpty();
        }

        /**
         * @brief The bone's base-pose local transform, as Animate left it, through the edit `token` opened.
         * @param[in] entity The skeleton entity.
         * @param[out] local The transform; the default when it reads nothing.
         * @return Whether it read: false for a stale token, an empty edit or a bone out of range.
         */
        bool EditBase(const CallContext& context, SkeletonScripting& skeletons, std::uint64_t token,
                      const Entity* entity, std::int32_t bone, Transform* local)
        {
            const PoseEdit edit = EditFor(skeletons, context, token, *entity);
            const std::span<const Transform> base = edit.Base();
            if (edit.IsEmpty() || !skeletons.InRange(context, bone, base.size()))
            {
                return false;
            }
            *local = base[static_cast<std::size_t>(bone)];
            return true;
        }

        /**
         * @brief The bone's current local transform in the final pose, with every write so far applied.
         * @param[in] entity The skeleton entity.
         * @param[out] local The transform; the default when it reads nothing.
         * @return Whether it read: false for a stale token, an empty edit or a bone out of range.
         */
        bool EditLocal(const CallContext& context, SkeletonScripting& skeletons, std::uint64_t token,
                       const Entity* entity, std::int32_t bone, Transform* local)
        {
            const PoseEdit edit = EditFor(skeletons, context, token, *entity);
            return !edit.IsEmpty() && skeletons.InRange(context, bone, edit.Base().size()) &&
                   Write(edit.Local(bone), local);
        }

        /**
         * @brief The bone's current model-space matrix, with every write so far applied.
         * @param[in] entity The skeleton entity.
         * @param[out] model The matrix; zero when it reads nothing.
         * @return Whether it read: false for a stale token, an empty edit or a bone out of range.
         */
        bool EditModel(const CallContext& context, SkeletonScripting& skeletons, std::uint64_t token,
                       const Entity* entity, std::int32_t bone, Math::Mat4* model)
        {
            const PoseEdit edit = EditFor(skeletons, context, token, *entity);
            return !edit.IsEmpty() && skeletons.InRange(context, bone, edit.Base().size()) &&
                   Write(edit.Model(bone), model);
        }

        /**
         * @brief Writes `target` to the bone now, as C++'s PoseEdit::Write does: later writes and modifiers see it.
         * @param[in] entity The skeleton entity.
         * @return Whether it wrote: false for a stale token, an empty edit, a bone out of range or an inverse that does
         * not exist (FenrirSkeleton warns of that one).
         */
        bool EditWrite(const CallContext& context, SkeletonScripting& skeletons, std::uint64_t token,
                       const Entity* entity, std::int32_t bone, const Target* target)
        {
            PoseEdit edit = EditFor(skeletons, context, token, *entity);
            return !edit.IsEmpty() && skeletons.InRange(context, bone, edit.Base().size()) && edit.Write(bone, *target);
        }

        /**
         * @brief Sets the bone's pose override entry, replacing any (ADR-0038): written each frame by PoseOverride.
         * @param[in] entity The skeleton entity.
         * @return Whether it was set: false when the entity is not Ready or the bone is out of range.
         * @facade Skeletons.SetOverride
         */
        bool SetOverride(const CallContext& context, SkeletonScripting& skeletons, const Entity* entity,
                         std::int32_t bone, const Target* target)
        {
            const std::optional<Entity> ready = skeletons.ReadyOf(context, *entity);
            return ready && Skeletons::SetOverride(context.GetApp().GetActiveScene(), *ready, bone, *target);
        }

        /**
         * @brief The bone's pose override entry, if it has one.
         * @param[in] entity The skeleton entity.
         * @param[out] target The entry; the default when it has none.
         * @return Whether it has one.
         * @facade Skeletons.GetOverride
         */
        bool GetOverride(const CallContext& context, SkeletonScripting& skeletons, const Entity* entity,
                         std::int32_t bone, Target* target)
        {
            const std::optional<Entity> ready = skeletons.ReadyOf(context, *entity);
            return ready && Write(Skeletons::GetOverride(context.GetApp().GetActiveScene(), *ready, bone), target);
        }

        /**
         * @brief Removes the bone's pose override entry; its override weight stays.
         * @param[in] entity The skeleton entity.
         * @return Whether it had one.
         * @facade Skeletons.ClearOverride
         */
        bool ClearOverride(const CallContext& context, SkeletonScripting& skeletons, const Entity* entity,
                           std::int32_t bone)
        {
            const std::optional<Entity> ready = skeletons.ReadyOf(context, *entity);
            return ready && Skeletons::ClearOverride(context.GetApp().GetActiveScene(), *ready, bone);
        }

        /**
         * @brief Removes every pose override entry of the skeleton entity; override weights stay.
         * @return Whether the entity is Ready.
         * @facade Skeletons.ClearOverrides
         */
        bool ClearOverrides(const CallContext& context, SkeletonScripting& skeletons, const Entity* entity)
        {
            const std::optional<Entity> ready = skeletons.ReadyOf(context, *entity);
            return ready && Skeletons::ClearOverrides(context.GetApp().GetActiveScene(), *ready);
        }

        /**
         * @brief Sets the bone's override weight, or with BoneSubtree the bone's and every descendant's, to `weight`
         * clamped to [0, 1]: an entry is written at its target's weight times this.
         * @param[in] entity The skeleton entity.
         * @return Whether it was set: false when the entity is not Ready or the bone is out of range.
         * @facade Skeletons.SetOverrideWeight
         */
        bool SetOverrideWeight(const CallContext& context, SkeletonScripting& skeletons, const Entity* entity,
                               std::int32_t bone, float weight, OverrideScope scope)
        {
            const std::optional<Entity> ready = skeletons.ReadyOf(context, *entity);
            return ready &&
                   Skeletons::SetOverrideWeight(context.GetApp().GetActiveScene(), *ready, bone, weight, scope);
        }

        /**
         * @brief The bone's override weight: 1 unless set, and 1 for an entity that is not Ready or a bone out of
         * range.
         * @param[in] entity The skeleton entity.
         * @facade Skeletons.GetOverrideWeight
         */
        float GetOverrideWeight(const CallContext& context, SkeletonScripting& skeletons, const Entity* entity,
                                std::int32_t bone)
        {
            const std::optional<Entity> ready = skeletons.ReadyOf(context, *entity);
            return ready ? Skeletons::GetOverrideWeight(context.GetApp().GetActiveScene(), *ready, bone) : 1.0f;
        }

        /**
         * @brief Sets the bone's override weight (or its subtree's) back to 1.
         * @param[in] entity The skeleton entity.
         * @return Whether it was set: false when the entity is not Ready or the bone is out of range.
         * @facade Skeletons.ClearOverrideWeight
         */
        bool ClearOverrideWeight(const CallContext& context, SkeletonScripting& skeletons, const Entity* entity,
                                 std::int32_t bone, OverrideScope scope)
        {
            const std::optional<Entity> ready = skeletons.ReadyOf(context, *entity);
            return ready && Skeletons::ClearOverrideWeight(context.GetApp().GetActiveScene(), *ready, bone, scope);
        }

        /**
         * @brief Whether the last Attach placed the entity at its bone, and why not (ADR-0037).
         * @facade Skeletons.GetAttachmentState
         */
        AttachmentState GetAttachmentState(const CallContext& context, SkeletonScripting& skeletons,
                                           const Entity* entity)
        {
            const NativeGuard guard(context, skeletons.warnings);
            const std::optional<Entity> alive = guard.Alive(*entity);
            return alive ? Skeletons::GetAttachmentState(context.GetApp().GetActiveScene(), *alive)
                         : AttachmentState::NotAttached;
        }

        /**
         * @brief Detaches the entity from its bone (C++'s Skeletons::Detach), when the system returns: a structural
         * change, so it is recorded and runs outside the pose pipeline, after the calling system or (from a stage
         * system) after the pass. KeepLocal removes the BoneAttachment only; KeepWorld reparents the entity under
         * `newParent` (no entity for the top of the scene), keeping where it is in the world. What it can tell now it
         * returns; a refusal at playback (a parent that died or cannot hold the transform) changes nothing and is
         * logged.
         * @return Detached when it was recorded; NotAttached for a dead entity or one with no BoneAttachment;
         * InvalidParent for a KeepWorld whose new parent is not alive.
         * @facade Skeletons.Detach
         */
        DetachResult Detach(const CallContext& context, SkeletonScripting& skeletons, const Entity* entity,
                            DetachMode mode, const Entity* newParent)
        {
            const NativeGuard guard(context, skeletons.warnings);
            const std::optional<Entity> alive = guard.Alive(*entity);
            if (!alive)
            {
                return DetachResult::NotAttached;
            }
            if (!alive->HasComponent<BoneAttachment>())
            {
                guard.Warn(std::string(SkeletonScripting::NotAttached));
                return DetachResult::NotAttached;
            }
            const bool parented = mode == DetachMode::KeepWorld && newParent->GetId() != Entity::Null;
            if (parented && !context.Entities().HasEntity(newParent->GetId()))
            {
                guard.Warn(std::string(SkeletonScripting::DeadParent));
                return DetachResult::InvalidParent;
            }
            const std::uint32_t id = alive->GetId();
            const std::optional<std::uint32_t> parent = parented ? std::optional(newParent->GetId()) : std::nullopt;
            context.Changes().RecordCall([id, mode, parent](App& app) {
                const Entity detached = HandleIn(app, id);
                const Entity under = parent ? HandleIn(app, *parent) : Entity{};
                if (parent && !under.IsValid())
                {
                    app.Logger()->Warn("[skeleton scripting] Skeletons.Detach of entity {} at playback: the new parent "
                                       "had died, so nothing changed",
                                       id);
                    return;
                }
                const DetachResult result = Skeletons::Detach(app.GetActiveScene(), detached, mode, under);
                if (result != DetachResult::Detached)
                {
                    app.Logger()->Warn(
                        "[skeleton scripting] Skeletons.Detach of entity {} at playback changed nothing: "
                        "{}",
                        id, ResultName(result));
                }
            });
            return DetachResult::Detached;
        }

        /**
         * @brief Spawns `model` into the active scene with its skeletons, for apps with no renderer (C++'s
         * Skeletons::SpawnModel): a root named after the file holding one entity per ordinary node with its Name and
         * Transform, under `parent` unless it is no entity, and at playback a SkeletonPose on each skeleton's owner and
         * bone attachments below bones. The root is usable at once (alive); the tree is made when the system returns.
         * @param model The model's handle.
         * @param parent The entity to spawn under; no entity for none. A parent that is not alive spawns nothing.
         * @param[out] root The model's root entity; no entity when nothing was spawned.
         * @return Whether it spawned: false with no asset store, a stale model or a parent not alive.
         * @facade Skeletons.SpawnModel returns root
         */
        bool SpawnModel(const CallContext& context, SkeletonScripting& skeletons, const AssetHandle<Model>* model,
                        const Entity* parent, Entity* root)
        {
            const NativeGuard guard(context, skeletons.warnings);
            AssetStore* store = Resolve<Model>(guard, *model);
            if (store == nullptr)
            {
                return false;
            }
            const bool parented = parent->GetId() != Entity::Null;
            const std::optional<Entity> under = parented ? guard.Alive(*parent) : std::nullopt;
            if (parented && !under)
            {
                return false;
            }
            // The nodes are made in a scene of their own by the store, as C++'s are, then recorded into the active
            // scene, which a native may not change directly (ADR-0029); the skeletons go on the recorded entities
            // through FenrirSkeleton's own SpawnSkeletons when they play back.
            Scene scratch(std::string(context.Native()));
            const SpawnedNodes spawned = store->SpawnNodes(scratch, *model);
            if (!spawned.root.IsValid())
            {
                return false;
            }
            std::unordered_map<std::uint32_t, std::uint32_t> made;
            const std::uint32_t top =
                Record(context.Changes(), spawned.root, under ? std::optional(under->GetId()) : std::nullopt, made);
            std::vector<std::optional<std::uint32_t>> nodes;
            for (const Entity& node : spawned.nodes)
            {
                const auto entity = node.IsValid() ? made.find(node.GetId()) : made.end();
                nodes.push_back(entity != made.end() ? std::optional(entity->second) : std::nullopt);
            }
            context.Changes().RecordCall([handle = *model, top, nodes = std::move(nodes)](App& app) {
                const SkinningSourceRegistration* skinning = app.TryGetResource<SkinningSourceRegistration>();
                if (skinning == nullptr)
                {
                    app.Logger()->Warn("{}", "[skeleton scripting] Skeletons.SpawnModel: FenrirSkeleton is not "
                                             "installed, so the spawned model's skeletons get no SkeletonPose");
                    return;
                }
                SpawnedNodes live;
                live.root = HandleIn(app, top);
                for (const std::optional<std::uint32_t>& node : nodes)
                {
                    live.nodes.push_back(node ? HandleIn(app, *node) : Entity{});
                }
                skinning->source->SpawnSkeletons(app.GetActiveScene(), handle, live);
            });
            *root = context.Entities().HandleOf(top);
            return true;
        }

        /**
         * @brief How many bones the skeleton asset has.
         * @return The count; 0 for an empty or stale handle.
         * @facade SkeletonData.BoneCount
         */
        std::int32_t BoneCount(const CallContext& context, SkeletonScripting& skeletons,
                               const AssetHandle<Skeleton>* skeleton)
        {
            return QuerySkeleton(context, skeletons, *skeleton, 0,
                                 [](const SkeletonData& data) { return static_cast<std::int32_t>(data.BoneCount()); });
        }

        /**
         * @brief The bone's parent in the skeleton asset.
         * @param[in] skeleton The skeleton asset.
         * @return The parent; -1 for a root, a bone out of range or a stale handle.
         * @facade SkeletonData.Parent
         */
        std::int32_t Parent(const CallContext& context, SkeletonScripting& skeletons,
                            const AssetHandle<Skeleton>* skeleton, std::int32_t bone)
        {
            return QuerySkeleton(context, skeletons, *skeleton, NoBone,
                                 [bone](const SkeletonData& data) { return data.Parent(bone); });
        }

        /**
         * @brief Whether `bone` lies below `ancestor` in the skeleton asset; a bone is not its own descendant.
         * @param[in] skeleton The skeleton asset.
         * @return False also when either is out of range or the handle is stale.
         * @facade SkeletonData.IsDescendant
         */
        bool IsDescendant(const CallContext& context, SkeletonScripting& skeletons,
                          const AssetHandle<Skeleton>* skeleton, std::int32_t bone, std::int32_t ancestor)
        {
            return QuerySkeleton(context, skeletons, *skeleton, false, [bone, ancestor](const SkeletonData& data) {
                return data.IsDescendant(bone, ancestor);
            });
        }

        /**
         * @brief Writes the bone's engine name (unique in the skeleton, what FindBone and bone names match) into
         * `name`, as much as fits.
         * @param[in] skeleton The skeleton asset.
         * @return The whole name's length in chars, so a caller can tell a cut-short copy; 0 for a bone out of range or
         * a stale handle.
         * @facade SkeletonData.EngineName
         */
        std::int32_t EngineName(const CallContext& context, SkeletonScripting& skeletons,
                                const AssetHandle<Skeleton>* skeleton, std::int32_t bone, char16_t* name,
                                std::int32_t capacity)
        {
            return QuerySkeleton(context, skeletons, *skeleton, 0, [&](const SkeletonData& data) {
                return WriteText(name, capacity, Utf8ToUtf16(data.EngineName(bone)));
            });
        }

        /**
         * @brief Writes the bone's name in its file into `name`, as much as fits; it may be empty or repeated.
         * @param[in] skeleton The skeleton asset.
         * @return The whole name's length in chars; 0 for a bone out of range or a stale handle.
         * @facade SkeletonData.SourceName
         */
        std::int32_t SourceName(const CallContext& context, SkeletonScripting& skeletons,
                                const AssetHandle<Skeleton>* skeleton, std::int32_t bone, char16_t* name,
                                std::int32_t capacity)
        {
            return QuerySkeleton(context, skeletons, *skeleton, 0, [&](const SkeletonData& data) {
                if (bone < 0 || static_cast<std::size_t>(bone) >= data.bones.size())
                {
                    return 0;
                }
                return WriteText(name, capacity, Utf8ToUtf16(data.bones[static_cast<std::size_t>(bone)].sourceName));
            });
        }

        /**
         * @brief Writes the bone and every bone below it, in index order (each after its parent), into `bones`, as
         * many as fit.
         * @param[in] skeleton The skeleton asset.
         * @return The whole subtree's size, so a caller can tell a cut-short copy; 0 for a bone out of range or a
         * stale handle.
         * @facade SkeletonData.Subtree
         */
        std::int32_t Subtree(const CallContext& context, SkeletonScripting& skeletons,
                             const AssetHandle<Skeleton>* skeleton, std::int32_t bone, std::int32_t* bones,
                             std::int32_t count)
        {
            return QuerySkeleton(context, skeletons, *skeleton, 0, [&](const SkeletonData& data) {
                const std::vector<std::int32_t> subtree = data.Subtree(bone);
                (void)CopyInto(std::span<const std::int32_t>(subtree), bones, count);
                return static_cast<std::int32_t>(subtree.size());
            });
        }
    } // namespace SkeletonNatives

    namespace
    {
        // Places the loaded script's groups in the pose stages: each at its order, after C++ work at an equal order,
        // its stage's context held while it runs; and the stage changes played back when each pass returns.
        void PlaceStages(const std::weak_ptr<SkeletonScripting>& weak, App& app, std::vector<StageGroup> groups,
                         StageChanges changes)
        {
            const std::shared_ptr<SkeletonScripting> state = weak.lock();
            if (!state)
            {
                return;
            }
            state->changes = std::move(changes);
            for (StageGroup& group : groups)
            {
                const std::string name = std::format("C# {} at {}", group.Stage(), group.Order());
                PoseStageHandle handle;
                if (group.Stage() == SkeletonScripting::AnimateStage)
                {
                    handle = Skeletons::HostAnimate(app, name, group.Order(), [weak, group](AnimateContext& context) {
                        const std::shared_ptr<SkeletonScripting> running = weak.lock();
                        if (!running)
                        {
                            return;
                        }
                        running->animate = &context;
                        group.Run();
                        running->animate = nullptr;
                    });
                }
                else if (group.Stage() == SkeletonScripting::ModifyStage)
                {
                    handle = Skeletons::HostModifier(app, name, group.Order(), [weak, group](ModifierContext& context) {
                        const std::shared_ptr<SkeletonScripting> running = weak.lock();
                        if (!running)
                        {
                            return;
                        }
                        running->modifier = &context;
                        ++running->editToken;
                        group.Run();
                        ++running->editToken;
                        running->modifier = nullptr;
                    });
                }
                else if (group.Stage() == SkeletonScripting::LateStage)
                {
                    handle = Skeletons::HostLate(app, name, group.Order(), [group](LateContext&) { group.Run(); });
                }
                if (handle.id == 0)
                {
                    app.Logger()->Info("{}", SkeletonScripting::NoPipeline);
                    state->placed.clear();
                    return;
                }
                state->placed.push_back(handle);
            }
            const PoseStageHandle playback = Skeletons::HostPassEnd(app, [weak](PoseTiming) {
                if (const std::shared_ptr<SkeletonScripting> running = weak.lock(); running && running->changes)
                {
                    running->changes->PlayBack();
                }
            });
            if (playback.id != 0)
            {
                state->placed.push_back(playback);
            }
        }

        void RemoveStages(const std::weak_ptr<SkeletonScripting>& weak, App& app)
        {
            const std::shared_ptr<SkeletonScripting> state = weak.lock();
            if (!state)
            {
                return;
            }
            for (const PoseStageHandle handle : state->placed)
            {
                (void)Skeletons::RemoveHosted(app, handle);
            }
            state->placed.clear();
            state->changes.reset();
        }
    } // namespace

    Fenrir::ScriptingExtension Skeletons::ScriptingExtension()
    {
        auto state = std::make_shared<SkeletonScripting>();
        const std::weak_ptr<SkeletonScripting> weak = state;
        Fenrir::ScriptingExtension extension = Generated::MakeSkeletonExtension(std::move(state));
        extension.stages = {std::string(SkeletonScripting::AnimateStage), std::string(SkeletonScripting::ModifyStage),
                            std::string(SkeletonScripting::LateStage)};
        extension.placeStages = [weak](App& app, std::vector<StageGroup> groups, StageChanges changes) {
            PlaceStages(weak, app, std::move(groups), std::move(changes));
        };
        extension.removeStages = [weak](App& app) { RemoveStages(weak, app); };
        return extension;
    }
} // namespace Fenrir
