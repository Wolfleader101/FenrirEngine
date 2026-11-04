#pragma once

#include <cstdint>
#include <span>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    class AssetStore;
    class EntityList;
    class Scene;
} // namespace Fenrir

// Material tools for the scenes: builtin:lit and builtin:unlit materials and boxes of them, materials drawn by the
// demo's own shaders (shaders/, embedded by fenrir_add_shaders), and images fetched to texture them.
namespace LightingDemo
{
    /** @brief A builtin:lit material of `colour`, `metallic` and `roughness`, glowing `emissive` times `strength`. */
    [[nodiscard]] Fenrir::AssetHandle<Fenrir::Material> LitMaterial(Fenrir::AssetStore& store, Fenrir::Scene& scene,
                                                                    Fenrir::Colour colour, float metallic,
                                                                    float roughness,
                                                                    Fenrir::Colour emissive = Fenrir::Colour::Black,
                                                                    float strength = 1.0f);

    /**
     * @brief A builtin:unlit material of `colour`, which glows it as it is: HDR, so it can bloom. White by default, so
     * an instance list's copies glow their copy colour.
     */
    [[nodiscard]] Fenrir::AssetHandle<Fenrir::Material> UnlitMaterial(Fenrir::AssetStore& store, Fenrir::Scene& scene,
                                                                      Fenrir::Colour colour = Fenrir::Colour::White);

    /** @brief An entity drawing `cube` in `material` at `position`, scaled by `scale`: a box, for a unit cube. */
    Fenrir::Entity Box(Fenrir::EntityList& entities, Fenrir::AssetHandle<Fenrir::Mesh> cube,
                       Fenrir::AssetHandle<Fenrir::Material> material, Fenrir::Math::Vec3 position,
                       Fenrir::Math::Vec3 scale);

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
