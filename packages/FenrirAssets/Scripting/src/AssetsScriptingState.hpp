#pragma once

#include <optional>
#include <source_location>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssetsScripting/AssetResolve.hpp"
#include "FenrirAssetsScripting/ScriptAssets.hpp"
#include "FenrirScripting/NativeGuard.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirScripting/WarnOnceRecord.hpp"

namespace Fenrir
{
    /**
     * @brief The assets extension's state, which every assets native function is given second: what they share, made
     * by Assets::ScriptingExtension and kept by the Scripting given the extension. In a header of its own because the
     * generated registration (Generated/FenrirAssetsRegistration.cpp) declares the natives, which name it. The lookups
     * every package may need, the store and a handle's asset, are the public AssetResolve.hpp's; what is here is only
     * Assets' own: where an asset is put. Each warns once per place and message through the native's guard.
     */
    struct AssetsScripting
    {
        /** @brief Where a native puts an asset: the store, and the owner C# named. */
        struct StoreAndOwner
        {
            AssetStore& store;
            AssetOwner owner;
        };

        /**
         * @brief The asset store and the owner C# names, or nothing when the app has no store or `owner` names
         * neither the scene nor the app; warns once for the caller's call site either way.
         */
        std::optional<StoreAndOwner> StoreAndOwnerFor(const NativeGuard& guard, ScriptAssetOwner owner,
                                                      std::source_location where = std::source_location::current())
        {
            return Placed(guard, StoreFor(guard, where), owner, where);
        }

        /**
         * @brief As StoreAndOwnerFor, when `handle` also has to name a live asset of kind `T` in the store (Resolve).
         */
        template <typename T>
        std::optional<StoreAndOwner> StoreAndOwnerFor(const NativeGuard& guard, const AssetHandle<T>& handle,
                                                      ScriptAssetOwner owner,
                                                      std::source_location where = std::source_location::current())
        {
            return Placed(guard, Resolve<T>(guard, handle, where), owner, where);
        }

        /** @brief The owner C# names: the active scene or the app. Warns once if it names neither. */
        std::optional<AssetOwner> OwnerOf(const NativeGuard& guard, ScriptAssetOwner owner,
                                          std::source_location where = std::source_location::current())
        {
            // Not a switch: `owner` comes from C#, so it may be none of the enumerators.
            if (owner == ScriptAssetOwner::Scene)
            {
                return AssetOwner(guard.Context().GetApp().GetActiveScene());
            }
            if (owner == ScriptAssetOwner::App)
            {
                return AssetOwner::App();
            }
            guard.Warn("no such asset owner, so it does nothing", where);
            return std::nullopt;
        }

        /** @brief What the natives have warned about, so that each place warns once. */
        WarnOnceRecord warnings{"assets scripting"};

      private:
        /** @brief `store` with the owner C# names, or nothing when either is missing. */
        std::optional<StoreAndOwner> Placed(const NativeGuard& guard, AssetStore* store, ScriptAssetOwner owner,
                                            std::source_location where)
        {
            if (store == nullptr)
            {
                return std::nullopt;
            }
            const std::optional<AssetOwner> assetOwner = OwnerOf(guard, owner, where);
            if (!assetOwner)
            {
                return std::nullopt;
            }
            return StoreAndOwner{*store, *assetOwner};
        }
    };
} // namespace Fenrir
