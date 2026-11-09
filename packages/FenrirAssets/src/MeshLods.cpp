// GenerateLods (ADR-0057): a mesh's chain of levels of detail through meshoptimizer, which only this file includes.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <meshoptimizer.h>

#include "FenrirAssets/MeshData.hpp"

namespace Fenrir
{
    namespace
    {
        constexpr std::size_t PositionStride = sizeof(Math::Vec3);
        // How much a normal's or a texture coordinate's change counts against a position's, both measured relative to
        // the mesh's extents: gltfpack's defaults.
        constexpr float NormalWeight = 0.5f;
        constexpr float UvWeight = 0.5f;

        // Per vertex, the simplifier's flags: a vertex whose position another vertex shares sits on an attribute seam
        // (a hard edge, a UV island's border), and is locked so the seam never moves.
        std::vector<unsigned char> SeamLocks(const MeshData& mesh)
        {
            const std::size_t vertices = mesh.positions.size();
            std::vector<unsigned int> remap(vertices);
            meshopt_generateVertexRemap(remap.data(), mesh.indices.data(), mesh.indices.size(), mesh.positions.data(),
                                        vertices, PositionStride);
            std::vector<std::uint32_t> verticesAtPosition(vertices, 0);
            for (std::size_t vertex = 0; vertex < vertices; ++vertex)
            {
                // ~0u marks a vertex no index names, which no triangle can carry anywhere.
                if (remap[vertex] != ~0u)
                {
                    ++verticesAtPosition[remap[vertex]];
                }
            }
            std::vector<unsigned char> locks(vertices, 0);
            for (std::size_t vertex = 0; vertex < vertices; ++vertex)
            {
                if (remap[vertex] != ~0u && verticesAtPosition[remap[vertex]] > 1)
                {
                    locks[vertex] = meshopt_SimplifyVertex_Lock;
                }
            }
            return locks;
        }

        // The attributes the simplifier weighs beside position, interleaved per vertex: the normal's three components
        // where the mesh has normals, then uv0's two where it has those. Empty when it has neither.
        struct Attributes
        {
            std::vector<float> values;
            std::vector<float> weights;
        };

        Attributes CollectAttributes(const MeshData& mesh)
        {
            Attributes attributes;
            const bool normals = !mesh.normals.empty();
            const bool uvs = !mesh.uv0.empty();
            if (normals)
            {
                attributes.weights.insert(attributes.weights.end(), 3, NormalWeight);
            }
            if (uvs)
            {
                attributes.weights.insert(attributes.weights.end(), 2, UvWeight);
            }
            attributes.values.reserve(mesh.positions.size() * attributes.weights.size());
            for (std::size_t vertex = 0; vertex < mesh.positions.size(); ++vertex)
            {
                if (normals)
                {
                    const Math::Vec3& normal = mesh.normals[vertex];
                    attributes.values.insert(attributes.values.end(), {normal.x, normal.y, normal.z});
                }
                if (uvs)
                {
                    const Math::Vec2& uv = mesh.uv0[vertex];
                    attributes.values.insert(attributes.values.end(), {uv.x, uv.y});
                }
            }
            return attributes;
        }

        std::vector<std::uint32_t> OptimisedForVertexCache(const std::vector<std::uint32_t>& indices,
                                                           std::size_t vertices)
        {
            std::vector<std::uint32_t> optimised(indices.size());
            meshopt_optimizeVertexCache(optimised.data(), indices.data(), indices.size(), vertices);
            return optimised;
        }
    } // namespace

    MeshData GenerateLods(const MeshData& mesh, const LodSettings& settings)
    {
        MeshData result = mesh;
        result.lods.clear();
        const std::size_t vertices = mesh.positions.size();
        // Nothing to simplify, or a mesh the store would refuse levels on (several submeshes) or refuse outright (an
        // index past its vertices, which meshoptimizer would read past too). A lone submesh must cover every index,
        // since reordering the indices for the vertex cache would otherwise scramble its range.
        const bool coversAll =
            mesh.submeshes.empty() || (mesh.submeshes.size() == 1 && mesh.submeshes.front().indexOffset == 0 &&
                                       mesh.submeshes.front().indexCount == mesh.indices.size());
        const bool inRange = std::all_of(mesh.indices.begin(), mesh.indices.end(),
                                         [&](std::uint32_t index) { return index < vertices; });
        if (mesh.indices.size() < 3 || mesh.indices.size() % 3 != 0 || vertices == 0 || !coversAll || !inRange)
        {
            return result;
        }

        const float* positions = &mesh.positions.front().x;
        result.indices = OptimisedForVertexCache(mesh.indices, vertices);

        const Attributes attributes = CollectAttributes(mesh);
        const std::vector<unsigned char> locks =
            attributes.weights.empty() ? std::vector<unsigned char>{} : SeamLocks(mesh);
        const float scale = meshopt_simplifyScale(positions, vertices, PositionStride);

        // The levels are reserved up front so `previous` stays valid as they are appended.
        result.lods.reserve(settings.maxLevels);
        const std::vector<std::uint32_t>* previous = &result.indices;
        float previousError = 0.0f;
        for (std::uint32_t level = 0; level < settings.maxLevels; ++level)
        {
            const std::size_t previousTriangles = previous->size() / 3;
            if (previousTriangles <= settings.minTriangles)
            {
                break;
            }
            const auto wanted =
                static_cast<std::size_t>(std::floor(static_cast<float>(previousTriangles) * settings.ratio));
            const std::size_t targetTriangles = std::max<std::size_t>(wanted, settings.minTriangles);

            // No error cap: the chain is cut by the triangle limits and the saving, and each level reports what it
            // cost. The destination needs room for the input's indices, since the simplifier can stop short.
            std::vector<std::uint32_t> simplified(previous->size());
            float relativeError = 0.0f;
            std::size_t count = 0;
            if (attributes.weights.empty())
            {
                // With no attributes there are no seams to lock: position alone drives the collapse order.
                count = meshopt_simplify(simplified.data(), previous->data(), previous->size(), positions, vertices,
                                         PositionStride, targetTriangles * 3, std::numeric_limits<float>::max(), 0,
                                         &relativeError);
            }
            else
            {
                count = meshopt_simplifyWithAttributes(
                    simplified.data(), previous->data(), previous->size(), positions, vertices, PositionStride,
                    attributes.values.data(), attributes.weights.size() * sizeof(float), attributes.weights.data(),
                    attributes.weights.size(), locks.data(), targetTriangles * 3, std::numeric_limits<float>::max(), 0,
                    &relativeError);
            }
            simplified.resize(count);

            // A level that keeps more than nine tenths of the triangles is not worth a draw of its own.
            const std::size_t triangles = count / 3;
            if (triangles == 0 || static_cast<float>(triangles) > 0.9f * static_cast<float>(previousTriangles))
            {
                break;
            }

            MeshLod lod;
            lod.indices = OptimisedForVertexCache(simplified, vertices);
            // The simplifier's error is this step's departure from the previous level, so the departure from full
            // detail is at most the steps summed (meshoptimizer's README says to accumulate them for selection). A
            // lossless step (duplicate triangles dropped, say) adds nothing; the chain still has to grow.
            lod.error = std::max(previousError + relativeError * scale,
                                 std::nextafter(previousError, std::numeric_limits<float>::infinity()));
            result.lods.push_back(std::move(lod));
            previous = &result.lods.back().indices;
            previousError = result.lods.back().error;
        }
        return result;
    }
} // namespace Fenrir
