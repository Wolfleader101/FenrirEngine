#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <vector>

#include "Device/CommandList.hpp"
#include "Device/GraphicsSurface.hpp"
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
        /** @brief Local to world; for a skinned item, its skeleton entity's world matrix. */
        Math::Mat4 world{1.0f};
        /**
         * @brief Whether it is skinned (ADR-0035): drawn with its shader's `SKINNED` variant from the palette entries
         * starting at `paletteOffset` in SceneFrame::palette. Only a mesh with skin data is drawn skinned.
         */
        bool skinned = false;
        /** @brief A skinned item's first palette entry, in bones. */
        std::uint32_t paletteOffset = 0;
        /**
         * @brief The world point a transparent item is sorted by; empty for its world origin. A skinned item's is the
         * centre of its skinned bounds.
         */
        std::optional<Math::Vec3> sortPoint;
    };

    /**
     * @brief One camera's view of the scene (ADR-0026): drawn into its own scene targets at its viewport's size, then
     * written by Output into its viewport of its render target.
     */
    struct ViewRequest
    {
        /** @brief World to view: the inverse of the camera's world matrix. */
        Math::Mat4 view{1.0f};
        /** @brief View to clip, reversed-Z (ReversedZPerspective or ReversedZOrthographic). */
        Math::Mat4 projection{1.0f};
        /** @brief The camera's world position. */
        Math::Vec3 position{0.0f};
        /** @brief What the Background pass draws where nothing opaque was drawn, linear, when `sky` is empty. */
        Colour background = Colour::Black;
        /**
         * @brief The Sky cubemap the Background pass draws instead, prepared with PrepareCubemap; empty for the colour.
         * One not prepared draws `background`.
         */
        AssetKey sky;
        /** @brief The render target image it draws into, prepared with PrepareTarget; empty for the window. */
        AssetKey target;
        /** @brief Its viewport in its target's pixels, from the top-left: at least one pixel, inside the target. */
        Graphics::PixelRect viewport;
        /** @brief Whether the view draws the scene's debug drawing in its `Debug` stage (Camera::drawDebug). */
        bool drawDebug = true;
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
        /**
         * @brief The skinning palettes the skinned draws read (ADR-0035), back to back, one skinning matrix (model x
         * inverse bind) per bone: written into the frame's palette buffer, whose entries the draws index by
         * `paletteOffset`.
         */
        std::vector<Math::Mat4> palette;
        /** @brief How many skeleton palettes `palette` holds back to back, each once however many draws read it. */
        std::uint32_t palettes = 0;
        /**
         * @brief The views, in order: views into one target draw in this order, the first clearing it. A view
         * sampling an image another view draws is drawn after it, unless that would close a cycle of views waiting on
         * each other, taking the views in this order: then the view gets the default texture there, logged once.
         */
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

    /**
     * @brief An orthographic projection with reversed-Z depth in [0, 1] (ADR-0026): `height` world units tall and
     * `height * aspect` wide, centred on the view axis, with depth 1 at `near` and 0 at `far` in front of the camera.
     */
    [[nodiscard]] Math::Mat4 ReversedZOrthographic(float height, float aspect, float near, float far);

    /**
     * @brief The pixels of a target `size` that a normalised viewport (fractions from the top-left) covers: its edges
     * rounded to the nearest pixel and clipped to the target. Empty (zero wide or high) when that is under one pixel.
     */
    [[nodiscard]] Graphics::PixelRect ViewportPixels(float x, float y, float width, float height,
                                                     Graphics::PixelSize size);
} // namespace Fenrir::Render
