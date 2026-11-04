#pragma once

#include <cstdint>
#include <vector>

#include "FenrirMath/Math.hpp"

namespace Fenrir::Render
{
    /** @brief The most cascades a view's sun shadow has (ADR-0050). */
    inline constexpr std::uint32_t MaxCascades = 4;

    /**
     * @brief The share of each cascade's depth range, at its far end, that blends into the next cascade's shadow (the
     * last cascade's into none), so no boundary shows as a seam.
     */
    inline constexpr float CascadeBlendShare = 0.1f;

    /** @brief The radius of the shadow filter's Vogel disk, in a cascade's texels. */
    inline constexpr float ShadowFilterRadius = 1.5f;

    /** @brief How every view's sun cascades are made, from RendererConfig. */
    struct CascadeConfig
    {
        /** @brief Cascades per view, 1 to MaxCascades. */
        std::uint32_t count = MaxCascades;
        /** @brief Each cascade's side in shadow-map texels; the view's atlas is `count` of them side by side. */
        std::uint32_t mapSize = 2048;
        /** @brief The practical split scheme's blend: 0 spaces the splits evenly, 1 logarithmically. */
        float lambda = 0.75f;
    };

    /**
     * @brief The view depths a camera's shadowed range [near, far] is split at (Zhang et al.'s practical scheme): for
     * split i of n, lambda x near (far / near)^(i / n) + (1 - lambda) x (near + (far - near) i / n). `count` + 1
     * values, from `near` to `far`.
     */
    [[nodiscard]] std::vector<float> CascadeSplits(float near, float far, std::uint32_t count, float lambda);

    /** @brief A view-space bounding sphere on the camera's axis: its centre `distance` in front, and its radius. */
    struct CascadeSphere
    {
        float distance = 0.0f;
        float radius = 0.0f;
    };

    /**
     * @brief The smallest sphere round the slice of a view's frustum between view depths `near` and `far`, centred on
     * its axis, its radius rounded up to a sixteenth of a unit. It depends on the projection alone, never on where the
     * camera is or which way it turns, so neither changes a cascade's size.
     */
    [[nodiscard]] CascadeSphere FitCascadeSphere(const Math::Mat4& projection, float near, float far);

    /** @brief The view depths a reversed-Z projection draws between: infinity for a perspective's far one. */
    struct DepthRange
    {
        float near = 0.0f;
        float far = 0.0f;
    };

    /** @brief The near and far planes of ReversedZPerspective's or ReversedZOrthographic's `projection`. */
    [[nodiscard]] DepthRange DepthRangeOf(const Math::Mat4& projection);

    /**
     * @brief World to the light's view space for light travelling along `direction` (unit length): looking along it,
     * with no translation. Fixed by the direction alone, so a cascade's texel grid turns only when the light does.
     */
    [[nodiscard]] Math::Mat4 LightRotation(Math::Vec3 direction);

    /** @brief One cascade of a view's sun shadow: what its shadow pass draws with, and how the view reads it. */
    struct Cascade
    {
        /** @brief World to the cascade's clip space, reversed-Z orthographic: what its shadow pass's view block holds.
         */
        Math::Mat4 view{1.0f};
        Math::Mat4 projection{1.0f};
        Math::Mat4 viewProjection{1.0f};
        /** @brief The view depth its shadow reaches to, past which the next cascade's takes over. */
        float split = 0.0f;
        /** @brief The world size of one of its texels. */
        float texelSize = 0.0f;
        /** @brief Its sphere in world space, snapped. */
        Math::Vec3 centre{0.0f};
        float radius = 0.0f;
    };

    /**
     * @brief A view's cascades for the sun travelling along `lightDirection`, out to `distance` from its camera (its
     * far plane, if nearer). Cascade i covers the view depths from split i, less the blend band of cascade i - 1, to
     * split i + 1, inside its sphere, which its map holds with a texel to spare on each side; the sphere's centre is
     * moved in the light's view space to whole texels (2 x radius / (mapSize - 2)), so as the camera moves the shadow
     * map's texels stay on the world's grid and shadow edges do not swim, and its radius never changes as the camera
     * turns, so they do not shimmer. Casters up to `distance` past the sphere towards the light are caught. Empty for
     * no distance or no cascades.
     */
    [[nodiscard]] std::vector<Cascade> FitCascades(const Math::Mat4& view, const Math::Mat4& projection,
                                                   Math::Vec3 lightDirection, float distance,
                                                   const CascadeConfig& config);

    /**
     * @brief World to the shadow atlas for cascade `index` of `count`: u and v (from the top) into its tile of the
     * atlas, `count` tiles across, and its reversed-Z depth.
     */
    [[nodiscard]] Math::Mat4 AtlasMatrix(const Cascade& cascade, std::uint32_t index, std::uint32_t count);
} // namespace Fenrir::Render
