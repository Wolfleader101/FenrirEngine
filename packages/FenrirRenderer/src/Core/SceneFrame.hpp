#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "Core/PunctualLights.hpp"
#include "Core/RectLights.hpp"
#include "Core/Tonemapping.hpp"
#include "Device/CommandList.hpp"
#include "Device/GraphicsSurface.hpp"
#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirMath/Frustum.hpp"
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
         * @brief An InstancedMeshRenderer's instance list (ADR-0051), prepared with PrepareInstanceList: drawn once per
         * copy, at `world` times the copy's transform, in one draw never merged with others. Empty for one item at
         * `world`; a list not prepared, or with no copies, draws nothing.
         */
        AssetKey instances;
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
        /**
         * @brief The item's world bounds when the facade knows them better than its mesh's box under `world`: a
         * skinned item's skinned bounds (ADR-0035). Empty for that box. Its draw record carries them, and a
         * transparent item is culled by them (ADR-0057). Not read for an instance list, whose bounds the core keeps.
         */
        std::optional<Math::AABB> bounds;
        /**
         * @brief Whether it is drawn into the sun's shadow cascades and the spot and point lights' shadow maps
         * (ADR-0050): an `Opaque` or `Masked` one only, a Masked one cut out by its alpha cutoff as the depth prepass
         * cuts it.
         */
        bool castShadows = true;
    };

    /** @brief A view's bloom (ADR-0049), the core's copy of Camera::bloom. */
    struct BloomSettings
    {
        /** @brief How much of the bloom SceneColor is mixed with, in [0, 1]; 0 or less adds no pass. */
        float intensity = 0.04f;
        /** @brief How far the bloom spreads, in [0, 1]: how much each wider level adds against the one above it. */
        float radius = 0.7f;
    };

    /** @brief A view's ambient occlusion (ADR-0049), the core's copy of Camera::ambientOcclusion. */
    struct AmbientOcclusionSettings
    {
        /** @brief Whether the view has GTAO passes: only with a radius and an intensity above 0 too. */
        bool enabled = false;
        /** @brief How far round a surface others occlude it, in view-space units: the world's for an unscaled camera.
         */
        float radius = 0.5f;
        /** @brief The power the occlusion is raised to is XeGTAO's 2.2 times this. */
        float intensity = 1.0f;
    };

    /** @brief A view's screen-space reflections (ADR-0056), the core's copy of Camera::reflections. */
    struct ReflectionSettings
    {
        /** @brief Whether the view keeps a history colour and Hi-Z pyramids, and adds their passes. */
        bool enabled = false;
        /** @brief How much of the reflection replaces the sky's prefiltered specular, times its confidence. */
        float intensity = 1.0f;
        /** @brief How far a mirror ray is traced, in world units. */
        float maxDistance = 50.0f;
        /** @brief How far, in view depth, a ray may pass behind a surface and still hit it. */
        float thickness = 0.5f;
        /** @brief The perceptual roughness at and above which surfaces reflect nothing. */
        float roughnessCutoff = 0.6f;
    };

    /**
     * @brief Whether a view with `settings` gets reflections: enabled, with an intensity and a max distance above 0.
     * Only then does it keep a history and pyramids, and add their passes and the trace.
     */
    [[nodiscard]] inline bool HasReflections(const ReflectionSettings& settings) noexcept
    {
        return settings.enabled && settings.intensity > 0.0f && settings.maxDistance > 0.0f;
    }

    /** @brief What a view's Output pass shows instead of its lit colour (Camera::debugView). */
    enum class ViewDebug : std::uint8_t
    {
        None,
        /** @brief Its ambient occlusion as grey, black without GTAO passes. */
        AmbientOcclusion,
        /** @brief Its lit colour with each pixel tinted by the sun's cascade it reads, white for none. */
        ShadowCascades,
        /** @brief Its sun's shadow atlas, the cascades side by side, as grey depths; black without shadows. */
        ShadowAtlas,
        /** @brief The frame's spot shadow atlas, its tiles as grey depths; black when no spot light casts. */
        SpotShadowAtlas,
        /** @brief Each lit pixel as the heat of how many lights its cluster lists (ADR-0049), or of every light. */
        LightClusters,
        /** @brief Last frame's history at each reflection hit, times its confidence (ADR-0056); black without. */
        Reflections,
        /** @brief Each reflection hit's confidence as grey; black without reflections. */
        ReflectionConfidence,
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
         * @brief The Sky cubemap or panorama the Background pass draws instead, prepared with PrepareCubemap or
         * PreparePanorama; empty for the colour. One not prepared draws `background`.
         */
        AssetKey sky;
        /** @brief The render target image it draws into, prepared with PrepareTarget; empty for the window. */
        AssetKey target;
        /** @brief Its viewport in its target's pixels, from the top-left: at least one pixel, inside the target. */
        Graphics::PixelRect viewport;
        /** @brief Whether the view draws the scene's debug drawing in its `Debug` stage (Camera::drawDebug). */
        bool drawDebug = true;
        /** @brief The view's exposure in EV stops (Camera::exposure): Output scales by 2^exposure. */
        float exposure = 0.0f;
        /** @brief How Output maps the exposed colour into its target (Camera::tonemapper). */
        Tonemapper tonemapper = Tonemapper::AgX;
        /** @brief How its `PostProcess` bloom mixes into SceneColor (Camera::bloom). */
        BloomSettings bloom;
        /** @brief Its GTAO, which builtin:lit's sky light is darkened by in `Opaque` (Camera::ambientOcclusion). */
        AmbientOcclusionSettings ambientOcclusion;
        /** @brief Its screen-space reflections' history and pyramids (Camera::reflections). */
        ReflectionSettings reflections;
        /**
         * @brief Its camera's entity id, which what the renderer keeps of the view across frames (its reflection
         * history, ADR-0056) is kept under: one view a frame per id.
         */
        std::uint32_t camera = 0;
        /** @brief What Output draws instead of SceneColor, or how its lit colour is tinted. */
        ViewDebug debugView = ViewDebug::None;
    };

    /** @brief The sun's shadow (ADR-0050), the core's copy of DirectionalLight's shadow members. */
    struct SunShadowSettings
    {
        /** @brief Whether it casts: the light's castShadows, and a light with colour. */
        bool enabled = false;
        /** @brief How far from each camera, along its axis, the cascades reach, in world units. */
        float distance = 100.0f;
        /** @brief How far a pixel moves along its normal before reading its cascade, in that cascade's texels. */
        float normalBias = 1.0f;
        /**
         * @brief How far it moves towards the light, in texels per unit of the slope (tan of the angle between its
         * normal and the light), at least a tenth of a unit of slope.
         */
        float slopeBias = 2.0f;
    };

    /** @brief The light every view of the scene is lit by, in the frame set (`Fenrir/Frame.glsl`). */
    struct SceneLighting
    {
        /** @brief The way the directional light travels, in world space, unit length. */
        Math::Vec3 lightDirection{0.0f, 0.0f, -1.0f};
        /** @brief The directional light's colour times its intensity, linear; black with no light. */
        Math::Vec3 lightColour{0.0f};
        /** @brief Its shadow, drawn into cascades for every view. */
        SunShadowSettings shadows;
        /**
         * @brief The cubemap or panorama of the sky the scene is lit by (ADR-0047): its SkyLight's, else its Sky's,
         * prepared with PrepareCubemap or PreparePanorama; empty for a colour, or one not prepared, which lights with
         * `skyColour`.
         */
        AssetKey sky;
        /** @brief Its colour, linear: the light from every side when `sky` is empty or not prepared. */
        Math::Vec3 skyColour{0.0f};
        /** @brief Its intensity and its rotation about +Y in radians: 1 and 0 for a ColourSky. */
        float skyIntensity = 1.0f;
        float skyRotation = 0.0f;
        /**
         * @brief The point and spot lights (ADR-0048): every one in the scene that can light anything. The light
         * buffer holds them in this order, but for the spot lights that keep their shadows this frame (ADR-0050),
         * which it holds first.
         */
        std::vector<PunctualLight> lights;
        /** @brief The rect lights (ADR-0048), written into the light buffer after `lights`. */
        std::vector<RectLight> rectLights;
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
        /**
         * @brief The intensity and rotation about +Y in radians of the Sky the views draw behind everything: 1 and 0
         * for a ColourSky. The same as the lighting's unless the scene has a SkyLight.
         */
        float backgroundIntensity = 1.0f;
        float backgroundRotation = 0.0f;
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
     * @brief The box around `box` under each of `copies` (ADR-0057): an instance list's bounds in its drawing entity's
     * local space, for the box of the mesh it draws. The zero box at the origin for no copies.
     */
    [[nodiscard]] Math::AABB CopiesBounds(std::span<const Math::Mat4> copies, const Math::AABB& box);

    /**
     * @brief The pixels of a target `size` that a normalised viewport (fractions from the top-left) covers: its edges
     * rounded to the nearest pixel and clipped to the target. Empty (zero wide or high) when that is under one pixel.
     */
    [[nodiscard]] Graphics::PixelRect ViewportPixels(float x, float y, float width, float height,
                                                     Graphics::PixelSize size);
} // namespace Fenrir::Render
