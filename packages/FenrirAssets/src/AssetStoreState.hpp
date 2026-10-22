#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "FenrirAssets/AssetStore.hpp"

namespace Fenrir::Detail
{
    /** @brief Who owns an asset, as the store records it: the app, or one scene's number. */
    using OwnerId = std::uint32_t;

    /** @brief The app's owner number. Scenes are numbered from one, in the order they first own an asset. */
    inline constexpr OwnerId AppOwnerId = 0;

    /** @brief One asset's place in its kind's table. Free slots keep their generation for the next asset. */
    struct AssetRecord
    {
        // Starts at one, so the empty handle (generation zero) never resolves; bumped each time the slot is freed.
        std::uint32_t generation = 1;
        // Zero while the slot is free.
        std::uint32_t revision = 0;
        AssetBox data{nullptr, nullptr};
        std::vector<OwnerId> owners;
        // The loaded path and label, as deduplication keys it; empty for an added asset.
        std::string key;
        // The problems already logged for this asset (a material's unknown parameter names), so each is logged once.
        std::unordered_set<std::string> logged;
    };

    /** @brief Every asset of one kind, and the loaded ones by path. */
    struct AssetTable
    {
        std::vector<AssetRecord> records;
        std::vector<std::uint32_t> freeIndices;
        std::unordered_map<std::string, std::uint32_t> byKey;
    };

    struct AssetStoreState
    {
        ILogger* logger = nullptr;
        std::filesystem::path root;
        std::array<AssetTable, AssetKindCount> tables;
        OwnerId nextSceneOwner = 1;

        /** @brief The live record `slot` names in `kind`'s table, or null. */
        [[nodiscard]] AssetRecord* Find(AssetKind kind, AssetSlot slot);

        /** @brief Frees the asset in `index` of `kind`'s table: its data, its path, and every handle to it. */
        void Free(AssetKind kind, std::uint32_t index);

        /** @brief Takes `owner` off every asset, freeing those it was the last owner of. */
        void ReleaseOwner(OwnerId owner);
    };

    /**
     * @brief Puts `data` in a free slot of `table` (or a new one) with `owner` as its only owner, at revision 1, and
     * files it under `key` unless the key is empty.
     */
    AssetSlot InsertAsset(AssetTable& table, AssetBox data, OwnerId owner, std::string key);

    /** @brief Reads the whole file into `bytes`; false if it could not be read. */
    bool ReadAssetFile(const std::filesystem::path& file, std::vector<std::byte>& bytes);

    /** @brief Adds `owner` to the record's owners, if it is not one already. */
    void AddOwner(AssetRecord& record, OwnerId owner);

    /** @brief The owner number of `owner` in the store `state`, making the scene an asset owner if it is not yet. */
    OwnerId OwnerIdOf(const std::shared_ptr<AssetStoreState>& state, AssetOwner owner);

    /**
     * @brief The scene resource that makes a scene an asset owner. Made the first time the scene owns an asset; when
     * the scene ends, it releases what the scene owned, if the store is still there.
     */
    struct SceneAssetOwner
    {
        std::weak_ptr<AssetStoreState> store;
        OwnerId id = 0;

        SceneAssetOwner(std::weak_ptr<AssetStoreState> owningStore, OwnerId ownerId)
            : store(std::move(owningStore)), id(ownerId)
        {
        }
        SceneAssetOwner(const SceneAssetOwner&) = delete;
        SceneAssetOwner& operator=(const SceneAssetOwner&) = delete;
        ~SceneAssetOwner()
        {
            if (const std::shared_ptr<AssetStoreState> state = store.lock())
            {
                state->ReleaseOwner(id);
            }
        }
    };
} // namespace Fenrir::Detail
