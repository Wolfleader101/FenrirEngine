#pragma once

#include <cstdint>
#include <span>

#include "FenrirAssets/AssetHandle.hpp"

namespace Fenrir
{
    class AssetStore;
    class Scene;
} // namespace Fenrir

// Material tools for the scenes drawn by the demo's own shaders (shaders/, embedded by fenrir_add_shaders) and textured
// by fetched images.
namespace LightingDemo
{
    /** @brief A material drawing with the embedded shader container `bytes`; empty if it does not load. */
    [[nodiscard]] Fenrir::AssetHandle<Fenrir::Material> EmbeddedMaterial(Fenrir::AssetStore& store,
                                                                         Fenrir::Scene& scene,
                                                                         std::span<const std::uint8_t> bytes);

    /**
     * @brief `image` again, read as linear: JPEGs load as sRGB, which a normal or ARM map is not. Empty if `image`
     * does not resolve.
     */
    [[nodiscard]] Fenrir::AssetHandle<Fenrir::Image> AsLinear(Fenrir::AssetStore& store, Fenrir::Scene& scene,
                                                              Fenrir::AssetHandle<Fenrir::Image> image);
} // namespace LightingDemo
