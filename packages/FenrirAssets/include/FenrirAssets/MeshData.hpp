#pragma once

#include <cstdint>
#include <vector>

#include "FenrirMath/Colour.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    /** @brief A range of a mesh's indices drawn with one of its materials. */
    struct Submesh
    {
        /** @brief The first index of the range, into MeshData::indices. */
        std::uint32_t indexOffset = 0;
        /** @brief How many indices the range holds: three per triangle. */
        std::uint32_t indexCount = 0;
        /** @brief Which of the mesh's materials draws the range: an index into its model's material list. */
        std::uint32_t materialSlot = 0;
    };

    /**
     * @brief A mesh as plain CPU-side data (ADR-0021): vertex streams, indices and submeshes, built in code or
     * imported.
     *
     * Every stream is optional; one that is present holds one value per vertex, as many as `positions` does. Triangles
     * are three indices each, counter-clockwise seen from the front. The store checks this when the mesh is added or
     * replaced, and refuses a mesh that breaks it.
     */
    struct MeshData
    {
        /** @brief Where each vertex is, in the mesh's local space. */
        std::vector<Math::Vec3> positions;
        /** @brief Each vertex's unit normal. */
        std::vector<Math::Vec3> normals;
        /** @brief Each vertex's unit tangent in `xyz`, with the bitangent's sign (+1 or -1) in `w`. */
        std::vector<Math::Vec4> tangents;
        /** @brief Each vertex's first texture coordinates. */
        std::vector<Math::Vec2> uv0;
        /** @brief Each vertex's second texture coordinates. */
        std::vector<Math::Vec2> uv1;
        /** @brief Each vertex's colour, linear. */
        std::vector<Colour> colours;
        /** @brief Indices into the vertex streams, three per triangle. */
        std::vector<std::uint32_t> indices;
        /** @brief The ranges of `indices` drawn with each material. */
        std::vector<Submesh> submeshes;
        /**
         * @brief The box around every position, in local space. The store computes it whenever the mesh is added or
         * replaced, overwriting what was here; with no positions it is a point at the origin.
         */
        Math::AABB bounds{Math::Vec3(0.0f), Math::Vec3(0.0f)};

        // Room for the animation spec's skin section (u16x4 joints, unorm16x4 weights, the bone count, the expected
        // skeleton signature and per-bone boxes) goes here; nothing reads or writes skinning yet.
    };
} // namespace Fenrir
