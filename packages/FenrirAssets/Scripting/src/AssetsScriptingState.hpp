#pragma once

#include <optional>
#include <source_location>
#include <string>
#include <string_view>

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
     * Assets' own: where an asset is put. Each warns once per place and message through the native's guard. The
     * texts Assets' own natives warn with are named here, one per rule, so the package's rules read in one place
     * and its rules tests name them; the store and handle rules' texts are AssetResolve.hpp's.
     */
    struct AssetsScripting
    {
        /** @brief Warned when `owner` is neither the scene nor the app: the native does nothing. */
        static constexpr std::string_view NoOwner = "no such asset owner, so it does nothing";

        /** @brief Warned when an asset kind C# names is none of AssetKind's: the native does nothing. */
        static constexpr std::string_view NoKind =
            "the handle's type is not Mesh, Image, Material, Shader or Model, so it does nothing";

        /** @brief Warned when an image's width or height is under one pixel: the native does nothing. */
        static constexpr std::string_view NoPixels = "an image is at least one pixel each way, so it does nothing";

        /** @brief Warned by ResizeRenderTarget when the image has pixels, so is no render target: it does nothing. */
        static constexpr std::string_view NotRenderTarget =
            "the image has pixels, so it is not a render target and it does nothing";

        /** @brief Warned by SetBlendMode when the mode is none of BlendMode's enumerators: it does nothing. */
        static constexpr std::string_view NoBlendMode = "no such blend mode, so it does nothing";

        /** @brief Warned by GetShaderParameter when the index is past the shader's parameters: it does nothing. */
        static constexpr std::string_view NoParameter = "the shader has no parameter at that index, so it does nothing";

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
            guard.Warn(std::string(NoOwner), where);
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
