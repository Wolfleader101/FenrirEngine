#pragma once

#include <compare>
#include <cstdint>
#include <vector>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir::Render
{
    /**
     * @brief The renderer core's name for an asset it keeps GPU resources for (ADR-0023): the asset handle's slot and
     * generation, so a handle that goes stale names a key the core frees. The core never sees the asset store.
     */
    struct AssetKey
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;

        /** @brief Whether this names no asset, as an empty handle does. */
        [[nodiscard]] constexpr bool IsEmpty() const noexcept
        {
            return generation == 0;
        }

        friend constexpr auto operator<=>(const AssetKey&, const AssetKey&) = default;
    };

    /** @brief The key of the asset `handle` names. */
    template <typename T>
    [[nodiscard]] constexpr AssetKey KeyOf(AssetHandle<T> handle) noexcept
    {
        return {handle.index, handle.generation};
    }

    /** @brief One MeshRenderer read from the scene: a mesh, a material and where it is. */
    struct DrawRequest
    {
        /** @brief A mesh the core has prepared; anything else is skipped. */
        AssetKey mesh;
        /** @brief A material the core has prepared; an empty or unknown one draws with the fallback material. */
        AssetKey material;
        /** @brief Local to world. */
        Math::Mat4 world{1.0f};
    };

    /** @brief One camera's view of the scene, drawn into the whole target. */
    struct ViewRequest
    {
        /** @brief World to view: the inverse of the camera's world matrix. */
        Math::Mat4 view{1.0f};
        /** @brief View to clip, reversed-Z (ReversedZPerspective). */
        Math::Mat4 projection{1.0f};
        /** @brief The camera's world position. */
        Math::Vec3 position{0.0f};
        /** @brief What the Background pass draws where nothing opaque was drawn, linear. */
        Colour background = Colour::Black;
    };

    /** @brief The light every view of the scene is lit by, in the frame block (`Fenrir/Frame.glsl`). */
    struct SceneLighting
    {
        /** @brief The way the directional light travels, in world space, unit length. */
        Math::Vec3 lightDirection{0.0f, 0.0f, -1.0f};
        /** @brief The directional light's colour times its intensity, linear; black with no light. */
        Math::Vec3 lightColour{0.0f};
        /** @brief The ambient light's colour times its intensity, linear. */
        Math::Vec3 ambient{0.0f};
    };

    /**
     * @brief What the ECS facade read from the active scene for one frame (ADR-0019, ADR-0023): the draws, the views
     * and the light. Plain data, nothing kept from the frame before, so a render thread could take it as its snapshot.
     */
    struct SceneFrame
    {
        std::vector<DrawRequest> draws;
        std::vector<ViewRequest> views;
        SceneLighting lighting;
        /** @brief Seconds since the app started, and this frame's delta. */
        float time = 0.0f;
        float deltaTime = 0.0f;
    };

    /**
     * @brief A perspective projection with an infinite far plane and reversed-Z depth in [0, 1] (ADR-0026): a point at
     * distance `near` in front of the camera gets depth 1, and depth falls to 0 with distance. Right-handed view space
     * looking along -Z, clip +Y up.
     */
    [[nodiscard]] Math::Mat4 ReversedZPerspective(float verticalFov, float aspect, float near);
} // namespace Fenrir::Render
