#pragma once

#include "FenrirAssets/MeshData.hpp"

// Mesh simplification for the scenes that draw tens of thousands of copies of a fetched mesh, which the renderer has
// no LODs for.
namespace LightingDemo
{
    /**
     * @brief `mesh` simplified by vertex clustering: its bounds cut into `cells` a side, each cell's vertices merged
     * into one at their mean position, normal and UV, and the triangles that collapse dropped. Poly Haven's rocks have
     * thousands of triangles each, which tens of thousands of copies cannot afford. Thin meshes (leaves, twigs) lose
     * their shape this way, so it is for solid ones.
     */
    [[nodiscard]] Fenrir::MeshData Simplified(const Fenrir::MeshData& mesh, int cells);
} // namespace LightingDemo
