#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "AssetSource.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirTypes/TypeDescription.hpp"

namespace Fenrir::Detail
{
    /** @brief Who owns an asset, as the store records it: the app, or one scene's number. */
    using OwnerId = std::uint32_t;

    /** @brief The app's owner number. Scenes are numbered from one, in the order they first own an asset. */
    inline constexpr OwnerId AppOwnerId = 0;

    /**
     * @brief A data asset's instance (ADR-0075): of its file's type, which may extend the type it was loaded as, so it
     * is made, and destroyed, through that type's description.
     */
    struct DataValue
    {
        const TypeDescription* type = nullptr;
        void* value = nullptr;

        /** @brief A value-initialised instance of `description`'s type. */
        explicit DataValue(const TypeDescription& description)
            : type(&description), value(::operator new(description.size, std::align_val_t{description.alignment}))
        {
            description.Construct(value);
        }
        DataValue(const DataValue&) = delete;
        DataValue& operator=(const DataValue&) = delete;
        ~DataValue()
        {
            type->Destroy(value);
            ::operator delete(value, std::align_val_t{type->alignment});
        }
    };

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
        // For a data asset, the canonical name of its value's type (its description's, so it outlives the record).
        std::string_view typeName;
        // For a data asset, what its file's read reported, which every later load of its path reports again, as a
        // model's left-out units are: the located diagnostics, and whether the bridge read its fields in part.
        std::vector<Serialisation::Diagnostic> loadDiagnostics;
        bool loadIncomplete = false;
        // The problems already logged for this asset (a material's unknown parameter names, a data asset read as a
        // type it is not), so each is logged once. Cleared when the slot is freed, so the next asset starts afresh.
        std::unordered_set<std::string> logged;
        // An image's last region updates (AssetStore::UpdateImage), each with the revision it made, oldest first.
        // Cleared whenever the whole image changes, so a revision before the first is one a copy cannot catch up from.
        std::deque<std::pair<std::uint32_t, ImageRegion>> imageUpdates;
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
        /** @brief The app's type registry, which data asset types are found and checked in. */
        const TypeRegistry* types = nullptr;
        std::filesystem::path root;
        /** @brief Where data asset envelopes are read from: the directory source over `root`, unless a test swapped it.
         */
        std::unique_ptr<AssetSource> source;
        std::array<AssetTable, AssetKindCount> tables;
        OwnerId nextSceneOwner = 1;
        /** @brief By model file key, the units its last import left out, for the files that left any out. */
        std::unordered_map<std::string, std::vector<std::string>> modelFailures;
        /**
         * @brief Guards a data asset record's `logged` while TryGet notes a read as a type the record is not: TryGet is
         * a read, which may run alongside others.
         */
        std::mutex readLogMutex;

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
