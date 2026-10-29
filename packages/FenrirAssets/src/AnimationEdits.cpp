// The store's edits of clip events and bone masks (ADR-0034, ADR-0036): each changes the asset in place and bumps its
// revision, so readers that cache what they derived (FenrirAnimation's bindings and masks) see the change.
#include <algorithm>
#include <cmath>
#include <format>
#include <string>

#include "AssetStoreState.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirLogger/ILogger.hpp"

namespace Fenrir
{
    std::optional<std::uint32_t> AssetStore::AddClipEvent(AssetHandle<Clip> clip, float time, std::string_view name)
    {
        Detail::AssetRecord* record =
            EditRecord(Detail::AssetKind::Clip, {clip.index, clip.generation}, "add a clip event");
        if (record == nullptr)
        {
            return std::nullopt;
        }
        auto& data = *static_cast<ClipData*>(record->data.get());
        if (!std::isfinite(time) || time < 0.0f || time > data.duration)
        {
            m_state->logger->Error("Could not add the clip event '{}': its time {} is outside the clip's {} seconds",
                                   name, time, data.duration);
            return std::nullopt;
        }
        // After any events already at this time, so equal times keep the order they were added in.
        const auto at = std::upper_bound(data.events.begin(), data.events.end(), time,
                                         [](float t, const ClipEvent& event) { return t < event.time; });
        const auto index = static_cast<std::uint32_t>(at - data.events.begin());
        data.events.insert(at, ClipEvent{time, std::string(name)});
        ++record->revision;
        return index;
    }

    bool AssetStore::RemoveClipEvent(AssetHandle<Clip> clip, std::uint32_t index)
    {
        Detail::AssetRecord* record =
            EditRecord(Detail::AssetKind::Clip, {clip.index, clip.generation}, "remove a clip event");
        if (record == nullptr)
        {
            return false;
        }
        auto& data = *static_cast<ClipData*>(record->data.get());
        if (index >= data.events.size())
        {
            m_state->logger->Error("Could not remove clip event {}: the clip has {} events", index, data.events.size());
            return false;
        }
        data.events.erase(data.events.begin() + index);
        ++record->revision;
        return true;
    }

    AssetHandle<BoneMask> AssetStore::CreateBoneMask(AssetOwner owner)
    {
        return Add<BoneMask>(BoneMaskData{}, owner);
    }

    bool AssetStore::IncludeInBoneMask(AssetHandle<BoneMask> mask, std::string_view boneName, float weight,
                                       MaskScope scope)
    {
        Detail::AssetRecord* record =
            EditRecord(Detail::AssetKind::BoneMask, {mask.index, mask.generation}, "include a bone in a bone mask");
        if (record == nullptr)
        {
            return false;
        }
        if (!std::isfinite(weight))
        {
            m_state->logger->Error("Could not include the bone '{}' in a bone mask: its weight is not finite",
                                   boneName);
            return false;
        }
        static_cast<BoneMaskData*>(record->data.get())
            ->entries.push_back(BoneMaskEntry{std::string(boneName), weight, scope});
        ++record->revision;
        return true;
    }

    bool AssetStore::ClearBoneMask(AssetHandle<BoneMask> mask)
    {
        Detail::AssetRecord* record =
            EditRecord(Detail::AssetKind::BoneMask, {mask.index, mask.generation}, "clear a bone mask");
        if (record == nullptr)
        {
            return false;
        }
        static_cast<BoneMaskData*>(record->data.get())->entries.clear();
        ++record->revision;
        return true;
    }
} // namespace Fenrir
