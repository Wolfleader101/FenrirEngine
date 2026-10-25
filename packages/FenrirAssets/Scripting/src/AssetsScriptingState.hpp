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
        /** @brief Logs `message` once for each place in the extension that notices a call doing nothing (warnings). */
        void WarnOnce(const CallContext& context, const std::string& message,
                      std::source_location where = std::source_location::current())
        {
            warnings.Warn(context.GetApp(), message, where);
        }

        /** @brief The app's asset store; warns once for the caller's call site if there is none. */
        AssetStore* StoreFor(const CallContext& context, std::string_view function,
                             std::source_location where = std::source_location::current())
        {
            if (AssetStore* store = context.GetApp().TryGetResource<AssetStore>())
            {
                return store;
            }
            WarnOnce(context,
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
            WarnOnce(context,
                     std::string(function) + ": the " + std::string(Detail::AssetTraits<T>::name) +
                         " handle is empty or stale, so it does nothing",
                     where);
            return nullptr;
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
            WarnOnce(context, std::string(function) + ": no such asset owner, so it does nothing", where);
            return std::nullopt;
        }

        /** @brief What the natives have warned about, so that each place warns once. */
        WarnOnceRecord warnings{"assets scripting"};
    };
} // namespace Fenrir
