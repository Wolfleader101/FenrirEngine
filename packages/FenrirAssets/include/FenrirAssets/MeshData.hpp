#pragma once

#include <array>
#include <cstdint>
#include <optional>
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

    /** @brief A vertex's four bone indices, or its four bone weights as unorm16 (65535 is 1), as the shader reads them.
     */
    using BoneQuad = std::array<std::uint16_t, 4>;

    /** @brief The sum every weighted vertex's four quantised bone weights come to exactly: 1 in unorm16. */
    inline constexpr std::uint32_t BoneWeightOne = 65535;

    /**
     * @brief `weights` normalised and quantised to unorm16 summing to exactly `BoneWeightOne` (ADR-0035): the rounding
     * remainder goes one unit at a time to the weights with the largest fractional parts, ties to the lower slot.
     * All-zero weights stay all zero. Empty for a negative or non-finite weight, which no skin may hold.
     */
    [[nodiscard]] std::optional<BoneQuad> QuantiseBoneWeights(Math::Vec4 weights);

    /**
     * @brief A skinned mesh's per-vertex bone data and what is derived from it (ADR-0035), stored in exactly the format
     * the shader reads: four `u16` bone indices and four `unorm16` weights per vertex. The mesh's positions, normals
     * and tangents are in skeleton bind space.
     */
    struct MeshSkin
    {
        /** @brief Each vertex's four bone indices, one per position. */
        std::vector<BoneQuad> joints;
        /**
         * @brief Each vertex's four bone weights, one per position, summing to exactly `BoneWeightOne`, or all zero for
         * a vertex that rides rigidly in bind space (with a warning). `QuantiseBoneWeights` makes them from floats.
         */
        std::vector<BoneQuad> weights;
        /**
         * @brief How many bones the mesh needs its skeleton to have. Left 0, the store sets it to the highest bone
         * index any vertex names, plus one; set, every index must be below it.
         */
        std::uint32_t boneCount = 0;
        /**
         * @brief The bind signature of the skeleton the mesh was baked against, which a skeleton it is drawn with must
         * match; 0 for unchecked (a mesh built in code may copy its skeleton's to opt in).
         */
        std::uint64_t expectedSignature = 0;
        /**
         * @brief Per bone, the box around every vertex with a positive weight for it, in skeleton bind space; empty for
         * a bone no vertex is weighted to. Moved by that bone's palette matrix (model x inverse bind), each bounds what
         * the bone can carry, so their union bounds the skinned mesh. The store computes them whenever the mesh is
         * added or replaced, overwriting what was here.
         */
        std::vector<std::optional<Math::AABB>> boneBounds;
        /** @brief The box around every vertex whose weights are all zero, in skeleton bind space; computed likewise. */
        std::optional<Math::AABB> unweightedBounds;
    };

    /**
     * @brief One level of detail of a mesh (ADR-0057): a coarser triangle list over the mesh's own vertex streams, so
     * every level shares the mesh's vertex buffer and a skinned mesh's levels share its skin.
     */
    struct MeshLod
    {
        /** @brief Indices into the mesh's vertex streams, three per triangle: fewer triangles than the level before. */
        std::vector<std::uint32_t> indices;
        /**
         * @brief The greatest distance, in the mesh's local units, by which this level departs from full detail.
         * Greater than the previous level's (level 0, the mesh's own `indices`, has error 0). The renderer projects it
         * to pixels to choose a level.
         */
        float error = 0.0f;
    };

    /** @brief What `GenerateLods` aims for; the defaults suit most meshes. */
    struct LodSettings
    {
        /** @brief The share of the previous level's triangles each level aims for. */
        float ratio = 0.5f;
        /** @brief How many levels to make at most, not counting the mesh's own full detail. */
        std::uint32_t maxLevels = 4;
        /** @brief No level is coarser than this many triangles; a level already at or below it is the last. */
        std::uint32_t minTriangles = 32;
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

        /** @brief The skin section, for a skinned mesh: its vertices are then in skeleton bind space. */
        std::optional<MeshSkin> skin;

        /**
         * @brief The mesh's levels of detail (ADR-0057), coarsest last; empty for a mesh drawn at full detail only.
         * Level 0 is `indices` itself, with error 0, and is not in the vector. The store refuses a level whose index
         * count is not whole triangles, whose index reaches past the positions, or whose error is not greater than the
         * previous level's, and refuses levels on a mesh with more than one submesh, since a level is one triangle
         * list with no material ranges. `GenerateLods` makes them.
         */
        std::vector<MeshLod> lods;
    };

    /**
     * @brief `mesh` with its levels of detail made by meshoptimizer (ADR-0057): each level aims at `settings.ratio` of
     * the previous level's triangles, and the chain stops at `settings.maxLevels`, when a level would start at or
     * below `settings.minTriangles`, or when simplifying saves less than a tenth of the triangles. Where the mesh has
     * normals or `uv0` they weigh in the error, and a vertex on an attribute seam (a position several vertices share)
     * is locked so seams stay where they were; a mesh with neither simplifies by position alone. Each level's `error`
     * is the sum, over the steps that led to it, of meshoptimizer's error scaled to the mesh's local units: a bound on
     * its departure from full detail, strictly growing. The mesh's own `indices` and each
     * level come back reordered for the GPU's vertex cache; every stream, the submeshes, the bounds and the skin are
     * kept as they were. A mesh comes back unchanged, with no levels, when it has no whole triangles, an index past its
     * positions, more than one submesh, or one submesh that does not cover all its indices.
     */
    [[nodiscard]] MeshData GenerateLods(const MeshData& mesh, const LodSettings& settings = {});
} // namespace Fenrir
