#pragma once

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirMath/Colour.hpp"

namespace Fenrir
{
    /**
     * @brief Draws one mesh with one material at its entity's world transform (ADR-0023): one MeshRenderer, one draw.
     * Plain data, read by the renderer from the active scene every Render; a mesh with several materials is several
     * child entities, each with its own MeshRenderer.
     *
     * An empty or stale mesh is skipped (a stale one logged once per handle). An empty or stale material draws with the
     * fallback material, flat magenta and unlit, logged once per handle. An entity with no Transform draws at the
     * origin.
     */
    struct MeshRenderer
    {
        AssetHandle<Mesh> mesh;
        AssetHandle<Material> material;
        /** @brief False hides this entity only; its children are drawn as their own MeshRenderers say. */
        bool visible = true;
    };

    /**
     * @brief A perspective projection with an infinite far plane and reversed-Z depth (ADR-0026): the near plane maps
     * to depth 1 and the distance to 0. The aspect comes from the target's pixel size.
     */
    struct Perspective
    {
        /** @brief The vertical field of view in radians; 60 degrees by default. */
        float verticalFov = 1.04719755f;
        /** @brief The distance to the near plane, in world units. */
        float near = 0.1f;
    };

    /**
     * @brief Draws a view of the active scene from its entity, looking along the entity's local -Z with +Y up
     * (ADR-0026). Every enabled camera in the active scene draws one view a frame, into the whole main window. With no
     * enabled camera the window is cleared to black, logged once.
     */
    struct Camera
    {
        Perspective projection;
        /** @brief False draws no view. */
        bool enabled = true;
    };

    /**
     * @brief The light shining along its entity's world -Z (ADR-0024). The renderer uses the first one in the active
     * scene and warns once if there are more.
     */
    struct DirectionalLight
    {
        /** @brief Linear. */
        Colour colour = Colour::White;
        /** @brief A plain multiplier on the colour. */
        float intensity = 1.0f;
    };

    /**
     * @brief The constant light every lit surface receives, a scene resource (ADR-0024). A scene without one is lit by
     * a default AmbientLight, a dim grey.
     */
    struct AmbientLight
    {
        /** @brief Linear. */
        Colour colour{0.1f, 0.1f, 0.1f, 1.0f};
        /** @brief A plain multiplier on the colour. */
        float intensity = 1.0f;
    };
} // namespace Fenrir
