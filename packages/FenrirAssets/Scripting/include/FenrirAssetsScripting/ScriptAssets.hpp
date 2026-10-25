#pragma once

// The plain data the assets natives take and give in C#'s form, which FenrirAssets's own types do not say: described
// from this binding's selection (api/FenrirAssetsScripting.api.toml), so their C# mirrors and mappings are generated
// (api/FenrirAssets.binding.toml). Public, because another package's binding that uses this one (FenrirRenderer's)
// includes the generated mappings, which include this header.

#include <cstdint>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirAssets/MaterialData.hpp"
#include "FenrirAssets/ShaderContainer.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
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

    /**
     * @brief Holds the enums the natives take by value that no shared struct holds, only so that they are described:
     * describe-api describes an enum through a field that uses it. The binding excludes this struct, so C# has its
     * enums and not it.
     */
    struct ScriptAssetEnums
    {
        Detail::AssetKind kind = Detail::AssetKind::Mesh;
        ScriptAssetOwner owner = ScriptAssetOwner::Scene;
        BlendMode blendMode = BlendMode::Opaque;
    };
} // namespace Fenrir
