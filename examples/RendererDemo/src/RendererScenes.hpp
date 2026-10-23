#pragma once

#include "FenrirDemoScaffold/DemoScaffold.hpp"

// RendererDemo's scenes, in the order of its scene list. Each renderer feature ticket adds its scene here.
namespace RendererDemo
{
    /** @brief Meshes and materials: a placeholder until renderer ticket 13 fills it; a lit cube under a light. */
    [[nodiscard]] FenrirDemo::DemoScene MeshesAndMaterials();
} // namespace RendererDemo
