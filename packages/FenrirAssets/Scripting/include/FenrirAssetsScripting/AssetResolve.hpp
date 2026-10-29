#pragma once

// How a native function finds the asset store and the asset a handle names, warning in Assets' words when it cannot:
// the "no store, then stale handle" rule, written once here so that another package's native that takes an asset
// handle (FenrirRenderer's SpawnModel) checks it as Assets' own natives do, under its own native's name. Each warns
// once per place and message through the guard (NativeGuard, ADR-0045); the place is the caller's unless it passes
// one, so a helper called from several natives can pass its caller's through. The two texts are named in
// AssetResolve, so a rules test of either package names them and copies nothing.

#include <source_location>
#include <string>
#include <string_view>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirScripting/NativeGuard.hpp"

namespace Fenrir
{
    /** @brief The texts the "no store, then stale handle" rule warns with. */
    namespace AssetResolve
    {
        /** @brief Warned when the app has no asset store: the native does nothing. */
        inline constexpr std::string_view NoStore =
            "the app has no asset store (FenrirAssets is not installed), so it does nothing";

        /**
         * @brief Warned when a handle of the kind log messages call `kindName` (Detail::AssetTraits<T>::name: "mesh",
         * "model", ...) is empty or stale: the native does nothing.
         */
        [[nodiscard]] inline std::string StaleHandle(std::string_view kindName)
        {
            return "the " + std::string(kindName) + " handle is empty or stale, so it does nothing";
        }
    } // namespace AssetResolve

    /** @brief The app's asset store, or null with a warning once if it has none (FenrirAssets is not installed). */
    [[nodiscard]] inline AssetStore* StoreFor(const NativeGuard& guard,
                                              std::source_location where = std::source_location::current())
    {
        return guard.Require(guard.Context().GetApp().TryGetResource<AssetStore>(), std::string(AssetResolve::NoStore),
                             where);
    }

    /**
     * @brief The asset store, if `handle` (as a native takes it, through its pointer) names a live asset of kind `T`
     * in it; null with a warning once if the app has no store or the handle is empty or stale.
     */
    template <typename T>
    [[nodiscard]] AssetStore* Resolve(const NativeGuard& guard, const AssetHandle<T>& handle,
                                      std::source_location where = std::source_location::current())
    {
        AssetStore* store = StoreFor(guard, where);
        if (store == nullptr || store->IsValid(handle))
        {
            return store;
        }
        guard.Warn(AssetResolve::StaleHandle(Detail::AssetTraits<T>::name), where);
        return nullptr;
    }
} // namespace Fenrir
