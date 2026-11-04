#pragma once

#include <cstddef>

#include "FenrirAssets/InstanceListData.hpp"
#include "FenrirAssets/MeshData.hpp"
#include "FenrirMath/Math.hpp"

// Mesh and instance-list tools for the scenes that draw thousands of copies of fetched meshes, which the renderer has
// no LODs for.
namespace LightingDemo
{
    /**
     * @brief `mesh` simplified by vertex clustering: its bounds cut into `cells` a side, each cell's vertices merged
     * into one at their mean position, normal and UV, and the triangles that collapse dropped. Poly Haven's rocks have
     * thousands of triangles each, which tens of thousands of copies cannot afford. Thin meshes (leaves, twigs) lose
     * their shape this way, so it is for solid ones; ThinnedCards is for foliage.
     */
    [[nodiscard]] Fenrir::MeshData Simplified(const Fenrir::MeshData& mesh, int cells);

    /**
     * @brief Foliage `mesh` with fewer cards, for a distant level of detail: its connected pieces (each leaf, twig or
     * needle card, joined by shared vertices) found, one in every `keepEvery` kept, spread evenly through the file's
     * order, and each kept piece grown by `grow` about its centre, so the crown keeps about its coverage with a
     * fraction of the triangles. Every stream the mesh has is kept; the pieces' order is unchanged.
     */
    [[nodiscard]] Fenrir::MeshData ThinnedCards(const Fenrir::MeshData& mesh, int keepEvery, float grow);

    /** @brief `list`'s first `count` copies, transforms and colours alike. */
    [[nodiscard]] Fenrir::InstanceListData FirstCopies(const Fenrir::InstanceListData& list, std::size_t count);

    /** @brief A transform placing a copy at `position`, turned by `rotation` and scaled by `scale`. */
    [[nodiscard]] Fenrir::Math::Mat4 Placement(Fenrir::Math::Vec3 position, Fenrir::Math::Quat rotation,
                                               Fenrir::Math::Vec3 scale);
} // namespace LightingDemo
