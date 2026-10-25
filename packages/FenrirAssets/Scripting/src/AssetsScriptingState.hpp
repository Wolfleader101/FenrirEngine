#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <set>
#include <source_location>
#include <string>
#include <string_view>
#include <tuple>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssetsScripting/ScriptAssets.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"

namespace Fenrir
{
    /**
     * @brief The assets extension's state, which every assets native function is given second: what they share, made
     * by Assets::ScriptingExtension and kept by the Scripting given the extension. In a header of its own because the
     * generated registration (Generated/FenrirAssetsRegistration.cpp) declares the natives, which name it.
     */
    struct AssetsScripting
    {
        /**
         * @brief A handle as it crosses to and from C#: one number, the generation in the high half and the index in
         * the low (C#'s AssetHandle<T>.Bits).
         */
        template <typename T>
        [[nodiscard]] static AssetHandle<T> Unpack(std::uint64_t bits) noexcept
        {
            return {static_cast<std::uint32_t>(bits), static_cast<std::uint32_t>(bits >> 32u)};
        }

        /** @brief See Unpack. */
        template <typename T>
        [[nodiscard]] static std::uint64_t Pack(AssetHandle<T> handle) noexcept
        {
            return (std::uint64_t{handle.generation} << 32u) | handle.index;
        }

        /**
         * @brief Logs `message` once for each place in the extension that notices a call doing nothing, and each
         * message, in debug builds only.
         */
        void WarnOnce(const CallContext& context, const std::string& message,
                      std::source_location where = std::source_location::current())
        {
#ifndef NDEBUG
            {
                const std::lock_guard<std::mutex> lock(mutex);
                if (!warned.emplace(where.file_name(), where.line(), where.column(), message).second)
                {
                    return;
                }
            }
            context.GetApp().Logger()->Warn("[assets scripting] {} (warned once)", message);
#else
            (void)context;
            (void)message;
            (void)where;
#endif
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
            if (store == nullptr || store->IsValid(Unpack<T>(bits)))
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

        /** @brief The calls warned about, by where and why. Natives may run on several threads at once. */
        std::mutex mutex;
        /** @brief See mutex. */
        std::set<std::tuple<std::string_view, std::uint_least32_t, std::uint_least32_t, std::string>> warned;
    };
} // namespace Fenrir
