#pragma once

#include <cstdint>

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

    /**
     * @brief Sky: a plinth of lit shapes under the scene's Sky, the skybox cubemap or a dusk colour, which the camera
     * can override with a colour of its own; keys switch the sky and the override.
     */
    [[nodiscard]] FenrirDemo::DemoScene Sky();

    /**
     * @brief Debug draw: every wire and solid shape, in a depth-tested row behind a wall and an on-top row through it,
     * with world-text labels; shapes relative to an orbiting sphere; screen text and keyed messages; keys toggle
     * groups, draw for three seconds, draw forever and clear.
     */
    [[nodiscard]] FenrirDemo::DemoScene DebugDrawing();

    /** @brief Meshes and materials (C#)'s number, which its C# systems know it by (scripts/MeshesAndMaterials.cs). */
    inline constexpr std::uint32_t MeshesAndMaterialsCSharpScene = 1;

    /**
     * @brief Meshes and materials (C#): a twin of Meshes and materials, the same scene with the same keys, spawned and
     * run entirely by C# systems (scripts/MeshesAndMaterials.cs). For an app with scripting.
     */
    [[nodiscard]] FenrirDemo::DemoScene MeshesAndMaterialsCSharp();

    /** @brief The name the stripes shader, embedded in the app, has for C#, which cannot load a shader by path. */
    inline constexpr const char* StripesShaderName = "builtin:renderer-demo-stripes";

    /** @brief Gives the app's asset store the shaders RendererDemo's C# scenes load by name (StripesShaderName). */
    void AddCSharpShaders(Fenrir::App& app);
} // namespace RendererDemo
