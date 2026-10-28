#pragma once

// The plain data the assets natives take and give in C#'s form, which FenrirAssets's own types do not say: described
// from this binding's selection (api/FenrirAssetsScripting.api.toml), so their C# mirrors and mappings are generated
// (api/FenrirAssets.binding.toml). A typed handle is not here: a native takes and gives an AssetHandle<T> through a
// pointer, as the bound type the binding already names (ADR-0044, amended by the asset-handles spec); only the three
// kind-generic natives, which know their kind at run time, take the kind-neutral ScriptAssetHandle below. Public,
// because another package's binding that uses this one (FenrirRenderer's) includes the generated mappings, which
// include this header.

#include <cstdint>

#include "FenrirAssets/ShaderContainer.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    // C#'s NativeAssetHandle (named so in the binding's [names], mirrored internal through its internal-structs):
    // the kind-generic natives' handle (Load, Unload, IsValid), which take FenrirAssets's Detail::AssetKind beside it
    // and convert it to and from AssetHandle<T> once the kind is known. Every other native takes the typed handle.
    /**
     * @brief An asset handle of no stated kind, laid out as AssetHandle<T> is: C#'s AssetHandle<T>.ToNative() and
     * FromNative(). Default is the empty handle.
     */
    struct ScriptAssetHandle
    {
        /** @brief The slot in the store's table of the kind. */
        std::uint32_t index = 0;
        /** @brief The slot's generation when the asset was made; zero only in the empty handle. */
        std::uint32_t generation = 0;
    };

    // C#'s AssetOwner (named so in the binding's [names]): the natives turn it into FenrirAssets's AssetOwner, which
    // holds the scene itself.
    /**
     * @brief Who an asset is loaded or added for. It lives until its last owner ends, or until it is unloaded; naming
     * an owner twice is the same as naming it once.
     */
    enum class ScriptAssetOwner : std::uint8_t
    {
        /** @brief The active scene: the asset is released when that scene is destroyed. */
        Scene,
        /** @brief The app: the asset lives as long as the asset store. */
        App,
    };

    // C#'s ShaderParameter (named so in the binding's [names]).
    /**
     * @brief One material parameter of a shader, with its parameter default: FenrirAssets's ShaderParameter without its
     * name, which `Shaders.GetParameter` writes to a span.
     */
    struct ScriptShaderParameter
    {
        ShaderParameterType type = ShaderParameterType::Float;
        /** @brief A number parameter's default: one value per component, the rest zero. Unused for a texture. */
        Math::Vec4 value{0.0f};
        /** @brief A texture parameter's default. Unused for a number. */
        DefaultTexture texture = DefaultTexture::White;
    };
} // namespace Fenrir
