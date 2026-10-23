#pragma once

#include "FenrirDemoScaffold/DemoScaffold.hpp"

// RendererDemo's scenes, in the order of its scene list. Each renderer feature ticket adds its scene here.
namespace RendererDemo
{
    /**
     * @brief Meshes and materials: the imported backpack, primitives in builtin:unlit and builtin:lit, a
     * metallic/roughness grid, the four blend modes and a double-sided quad, a user shader, the magenta fallback and an
     * empty and a stale mesh; keys turn the light, change the ambient, hide the backpack and edit a shared material.
     */
    [[nodiscard]] FenrirDemo::DemoScene MeshesAndMaterials();

    /**
     * @brief Cameras and render targets: split screen, an orthographic top-down minimap and a monitor showing a third
     * camera's image target; keys cycle the layout, the minimap's order and the main views' projection.
     */
    [[nodiscard]] FenrirDemo::DemoScene CamerasAndTargets();
} // namespace RendererDemo
