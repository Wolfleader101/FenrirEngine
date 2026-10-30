#include "FenrirAnimationScripting/AnimationScripting.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "AnimationScriptingState.hpp"
#include "FenrirAnimation/Animation.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/BoneMaskData.hpp"
#include "FenrirAssets/ClipData.hpp"
#include "FenrirAssetsScripting/AssetResolve.hpp"
#include "FenrirAssetsScripting/ScriptAssets.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirScripting/NativeGuard.hpp"
#include "FenrirScripting/NativeMarshalling.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "Generated/FenrirAnimationNatives.hpp"

// Everything C# sees of animation is generated (api/FenrirAnimation.binding.toml): the components', events' and shared
// struct's mappings, checked against the Fenrir.Animation contract whenever a script assembly loads, and from the
// native functions below (described into api/FenrirAnimation.natives.json) their C# declarations, AnimationNative, and
// this registration of them all.
namespace Fenrir::Generated
{
    void RegisterAnimation(ScriptingExtension& extension);
} // namespace Fenrir::Generated

namespace Fenrir
{
    namespace
    {
        // The clip `clip` names: its data, or null with a warning once if the app has no store or the handle is stale.
        const ClipData* ClipOf(const NativeGuard& guard, const AssetHandle<Clip>& clip)
        {
            AssetStore* store = Resolve<Clip>(guard, clip);
            return store == nullptr ? nullptr : store->TryGet(clip);
        }

        // The clip's event at `index`, or null with a warning once if it has none there.
        const ClipEvent* EventOf(const NativeGuard& guard, const ClipData& clip, std::uint32_t index)
        {
            if (index < clip.events.size())
            {
                return &clip.events[index];
            }
            guard.Warn(std::string(AnimationScripting::NoSuchEvent));
            return nullptr;
        }
    } // namespace

    // The native functions behind Fenrir.Animation's Animation, Clips and BoneMasks, each named as C# calls it.
    // tools/describe-api reads them from this file, in this order, and Iris.Bindings generates their C# declarations
    // (Fenrir.Animation.AnimationNative) and their registration from that. Each trusts the declaration (ADR-0045), and
    // with no asset store, a stale handle, an entity that is not alive or an index out of range does nothing and
    // returns false, 0 or nothing, warning once per call site in a debug build under its own name. FenrirAnimation not
    // installed reads as nothing playing. Not in an anonymous namespace: the generated natives header declares them.
    namespace AnimationNatives
    {
        /**
         * @brief Queues a seek of the entity's playback cursor to `seconds` (C++'s Animation::Seek): the skeleton
         * entity for its Animator, or a layer entity. Applied at its next evaluation, after any clip change; crosses no
         * events, and works while paused.
         * @return Whether it was queued: false for an entity that is not alive.
         * @facade Animation.Seek
         */
        bool Seek(const CallContext& context, AnimationScripting& animation, const Entity* entity, double seconds)
        {
            const NativeGuard guard(context, animation.warnings);
            const std::optional<Entity> alive = guard.Alive(*entity);
            if (!alive)
            {
                return false;
            }
            Animation::Seek(context.GetApp().GetActiveScene(), *alive, seconds);
            return true;
        }

        /**
         * @brief Where the entity's playback stands: its Animator's, or else its AnimationLayer's.
         * @param[out] info The playback; Missing with nothing playing when it reads nothing.
         * @return Whether the entity plays anything: false when its state is Missing.
         * @facade Animation.Playback
         */
        bool Playback(const CallContext& context, AnimationScripting& animation, const Entity* entity,
                      PlaybackInfo* info)
        {
            const NativeGuard guard(context, animation.warnings);
            const std::optional<Entity> alive = guard.Alive(*entity);
            *info = alive ? Animation::Playback(context.GetApp().GetActiveScene(), *alive) : PlaybackInfo{};
            return info->state != PoseState::Missing;
        }

        /**
         * @brief How long the clip plays, in seconds.
         * @return The duration; 0 for an empty or stale handle.
         * @facade Clips.Duration
         */
        float Duration(const CallContext& context, AnimationScripting& animation, const AssetHandle<Clip>* clip)
        {
            const NativeGuard guard(context, animation.warnings);
            const ClipData* data = ClipOf(guard, *clip);
            return data == nullptr ? 0.0f : data->duration;
        }

        /**
         * @brief How many timed events the clip has, in time order.
         * @return The count; 0 for an empty or stale handle.
         * @facade Clips.EventCount
         */
        std::int32_t EventCount(const CallContext& context, AnimationScripting& animation,
                                const AssetHandle<Clip>* clip)
        {
            const NativeGuard guard(context, animation.warnings);
            const ClipData* data = ClipOf(guard, *clip);
            return data == nullptr ? 0 : static_cast<std::int32_t>(data->events.size());
        }

        /**
         * @brief The time of the clip's event at `index`.
         * @param[out] time Its authored time in seconds; 0 when there is none.
         * @return Whether the clip has an event there.
         * @facade Clips.GetEvent
         */
        bool GetEvent(const CallContext& context, AnimationScripting& animation, const AssetHandle<Clip>* clip,
                      std::uint32_t index, float* time)
        {
            const NativeGuard guard(context, animation.warnings);
            const ClipData* data = ClipOf(guard, *clip);
            const ClipEvent* event = data == nullptr ? nullptr : EventOf(guard, *data, index);
            if (event == nullptr)
            {
                return false;
            }
            *time = event->time;
            return true;
        }

        /**
         * @brief Writes the name of the clip's event at `index` into `name`, as much as fits.
         * @return The whole name's length in chars, so a caller can tell a cut-short copy; 0 when there is no event.
         * @facade Clips.EventName
         */
        std::int32_t EventName(const CallContext& context, AnimationScripting& animation, const AssetHandle<Clip>* clip,
                               std::uint32_t index, char16_t* name, std::int32_t capacity)
        {
            const NativeGuard guard(context, animation.warnings);
            const ClipData* data = ClipOf(guard, *clip);
            const ClipEvent* event = data == nullptr ? nullptr : EventOf(guard, *data, index);
            return event == nullptr ? 0 : WriteText(name, capacity, Utf8ToUtf16(event->name));
        }

        /**
         * @brief Writes the index of every event of the clip named exactly `name`, in time order, into `indices`, as
         * many as fit.
         * @return How many events have that name, so a caller can tell a cut-short copy; 0 for a stale handle.
         * @facade Clips.FindEvents
         */
        std::int32_t FindEvents(const CallContext& context, AnimationScripting& animation,
                                const AssetHandle<Clip>* clip, std::string_view name, std::uint32_t* indices,
                                std::int32_t count)
        {
            const NativeGuard guard(context, animation.warnings);
            const ClipData* data = ClipOf(guard, *clip);
            if (data == nullptr)
            {
                return 0;
            }
            const std::vector<std::uint32_t> found = data->FindEvents(name);
            const std::span<std::uint32_t> room = SpanOf(indices, count);
            std::copy_n(found.begin(), std::min(room.size(), found.size()), room.begin());
            return static_cast<std::int32_t>(found.size());
        }

        /**
         * @brief Adds an event named `name` at `time` to the clip, after any events already at that time, bumping its
         * revision (ADR-0034).
         * @param[out] index The new event's index; 0 when nothing was added.
         * @return Whether it was added: false for a stale handle, or a time that is not finite or lies outside the
         * clip's duration (the store logs which).
         * @facade Clips.AddEvent
         */
        bool AddEvent(const CallContext& context, AnimationScripting& animation, const AssetHandle<Clip>* clip,
                      float time, std::string_view name, std::uint32_t* index)
        {
            const NativeGuard guard(context, animation.warnings);
            AssetStore* store = Resolve<Clip>(guard, *clip);
            if (store == nullptr)
            {
                return false;
            }
            const std::optional<std::uint32_t> added = store->AddClipEvent(*clip, time, name);
            if (!added)
            {
                guard.Warn(std::string(AnimationScripting::Refused));
                return false;
            }
            *index = *added;
            return true;
        }

        /**
         * @brief Removes the clip's event at `index`, bumping its revision; later events move down one.
         * @return Whether there was one to remove.
         * @facade Clips.RemoveEvent
         */
        bool RemoveEvent(const CallContext& context, AnimationScripting& animation, const AssetHandle<Clip>* clip,
                         std::uint32_t index)
        {
            const NativeGuard guard(context, animation.warnings);
            AssetStore* store = Resolve<Clip>(guard, *clip);
            const ClipData* data = store == nullptr ? nullptr : store->TryGet(*clip);
            return data != nullptr && EventOf(guard, *data, index) != nullptr && store->RemoveClipEvent(*clip, index);
        }

        /**
         * @brief Makes a new bone mask with no entries for `owner`: on a layer it covers no bone until one is
         * included.
         * @param[out] mask The new mask; the empty handle when none was made.
         * @return Whether one was made: false with no asset store or an owner that is neither.
         * @facade BoneMasks.Create returns mask
         */
        bool Create(const CallContext& context, AnimationScripting& animation, ScriptAssetOwner owner,
                    AssetHandle<BoneMask>* mask)
        {
            const NativeGuard guard(context, animation.warnings);
            AssetStore* store = StoreFor(guard);
            if (store == nullptr)
            {
                return false;
            }
            // Not a switch: `owner` comes from C#, so it may be none of the enumerators.
            std::optional<AssetOwner> resolved;
            if (owner == ScriptAssetOwner::Scene)
            {
                resolved = AssetOwner(context.GetApp().GetActiveScene());
            }
            else if (owner == ScriptAssetOwner::App)
            {
                resolved = AssetOwner::App();
            }
            if (!resolved)
            {
                guard.Warn(std::string(AnimationScripting::NoOwner));
                return false;
            }
            *mask = store->CreateBoneMask(*resolved);
            return !mask->IsEmpty();
        }

        /**
         * @brief Appends an entry giving the bone named `bone` (by engine name), or with Subtree its whole subtree,
         * `weight`; it overrides earlier entries for the bones it covers, and bumps the mask's revision.
         * @return Whether it was added: false for a stale handle or a weight that is not finite.
         * @facade BoneMasks.Include
         */
        bool Include(const CallContext& context, AnimationScripting& animation, const AssetHandle<BoneMask>* mask,
                     std::string_view bone, float weight, MaskScope scope)
        {
            const NativeGuard guard(context, animation.warnings);
            AssetStore* store = Resolve<BoneMask>(guard, *mask);
            if (store == nullptr)
            {
                return false;
            }
            if (!store->IncludeInBoneMask(*mask, bone, weight, scope))
            {
                guard.Warn(std::string(AnimationScripting::Refused));
                return false;
            }
            return true;
        }

        /**
         * @brief Removes every entry of the mask, bumping its revision.
         * @return Whether the handle named a mask.
         * @facade BoneMasks.Clear
         */
        bool Clear(const CallContext& context, AnimationScripting& animation, const AssetHandle<BoneMask>* mask)
        {
            const NativeGuard guard(context, animation.warnings);
            AssetStore* store = Resolve<BoneMask>(guard, *mask);
            return store != nullptr && store->ClearBoneMask(*mask);
        }
    } // namespace AnimationNatives

    Fenrir::ScriptingExtension Animation::ScriptingExtension()
    {
        return Generated::MakeAnimationExtension(std::make_shared<AnimationScripting>());
    }
} // namespace Fenrir
