#pragma once

#include <cstdint>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssetsScripting/AssetHandleBits.hpp"
#include "FenrirAssetsScripting/ScriptAssets.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirScripting/WarnOnceRecord.hpp"

namespace Fenrir
{
    /**
     * @brief The assets extension's state, which every assets native function is given second: what they share, made
     * by Assets::ScriptingExtension and kept by the Scripting given the extension. In a header of its own because the
     * generated registration (Generated/FenrirAssetsRegistration.cpp) declares the natives, which name it.
     */
    struct AssetsScripting
    {
        /** @brief The app's asset store; warns once for the caller's call site if there is none. */
        AssetStore* StoreFor(const CallContext& context, std::string_view function,
                             std::source_location where = std::source_location::current())
        {
            if (AssetStore* store = context.GetApp().TryGetResource<AssetStore>())
            {
                return store;
            }
            warnings.Warn(context,
                          std::string(function) + ": the app has no asset store (FenrirAssets is not installed), so it "
                                                  "does nothing",
                          where);
            return nullptr;
        }

        /**
         * @brief The asset store, if `bits` names a live asset of kind `T` in it; warns once for the caller's call site
         * if not.
         */
        template <typename T>
        AssetStore* Resolve(const CallContext& context, std::uint64_t bits, std::string_view function,
                            std::source_location where = std::source_location::current())
        {
            AssetStore* store = StoreFor(context, function, where);
            if (store == nullptr || store->IsValid(AssetHandleFromBits<T>(bits)))
            {
                return store;
            }
            warnings.Warn(context,
                          std::string(function) + ": the " + std::string(Detail::AssetTraits<T>::name) +
                              " handle is empty or stale, so it does nothing",
                          where);
            return nullptr;
        }

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
        std::optional<StoreAndOwner> StoreAndOwnerFor(const CallContext& context, ScriptAssetOwner owner,
                                                      std::string_view function,
                                                      std::source_location where = std::source_location::current())
        {
            return Placed(context, StoreFor(context, function, where), owner, function, where);
        }

        /**
         * @brief As StoreAndOwnerFor, when `bits` also has to name a live asset of kind `T` in the store (Resolve).
         */
        template <typename T>
        std::optional<StoreAndOwner> StoreAndOwnerFor(const CallContext& context, std::uint64_t bits,
                                                      ScriptAssetOwner owner, std::string_view function,
                                                      std::source_location where = std::source_location::current())
        {
            return Placed(context, Resolve<T>(context, bits, function, where), owner, function, where);
        }

        /** @brief The owner C# names: the active scene or the app. Warns once if it names neither. */
        std::optional<AssetOwner> OwnerOf(const CallContext& context, ScriptAssetOwner owner, std::string_view function,
                                          std::source_location where = std::source_location::current())
        {
            // Not a switch: `owner` comes from C#, so it may be none of the enumerators.
            if (owner == ScriptAssetOwner::Scene)
            {
                return AssetOwner(context.GetApp().GetActiveScene());
            }
            if (owner == ScriptAssetOwner::App)
            {
                return AssetOwner::App();
            }
            warnings.Warn(context, std::string(function) + ": no such asset owner, so it does nothing", where);
            return std::nullopt;
        }

        /** @brief What the natives have warned about, so that each place warns once. */
        WarnOnceRecord warnings{"assets scripting"};

      private:
        /** @brief `store` with the owner C# names, or nothing when either is missing. */
        std::optional<StoreAndOwner> Placed(const CallContext& context, AssetStore* store, ScriptAssetOwner owner,
                                            std::string_view function, std::source_location where)
        {
            if (store == nullptr)
            {
                return std::nullopt;
            }
            const std::optional<AssetOwner> assetOwner = OwnerOf(context, owner, function, where);
            if (!assetOwner)
            {
                return std::nullopt;
            }
            return StoreAndOwner{*store, *assetOwner};
        }
    };
} // namespace Fenrir
