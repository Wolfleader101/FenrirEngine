#pragma once

#include "FenrirDemoScaffold/DemoScaffold.hpp"

// RendererDemo's scenes, in the order of its scene list. Each renderer feature ticket adds its scene here.
namespace RendererDemo
{
    /** @brief Meshes and materials: a placeholder until renderer ticket 13 fills it; a lit cube under a light. */
    [[nodiscard]] FenrirDemo::DemoScene MeshesAndMaterials();

    /**
     * @brief Cameras and render targets: split screen, an orthographic top-down minimap and a monitor showing a third
     * camera's image target; keys cycle the layout, the minimap's order and the main views' projection.
     */
    [[nodiscard]] FenrirDemo::DemoScene CamerasAndTargets();
} // namespace RendererDemo
