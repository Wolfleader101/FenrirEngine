#include "SimplifiedMesh.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace LightingDemo
{
    Fenrir::MeshData Simplified(const Fenrir::MeshData& mesh, int cells)
    {
        using Fenrir::Math::Vec3;
        const Vec3 extent = glm::max(mesh.bounds.max - mesh.bounds.min, Vec3(1e-6f));
        const auto cellOf = [&](Vec3 position) {
            const glm::ivec3 cell =
                glm::clamp(glm::ivec3((position - mesh.bounds.min) / extent * static_cast<float>(cells)), 0, cells - 1);
            return static_cast<std::uint32_t>(cell.x + cells * (cell.y + cells * cell.z));
        };
        std::vector<std::int64_t> clusterOf(static_cast<std::size_t>(cells) * cells * cells, -1);
        std::vector<std::uint32_t> remap(mesh.positions.size());
        std::vector<std::uint32_t> counts;
        Fenrir::MeshData simple;
        const bool hasUv = mesh.uv0.size() == mesh.positions.size();
        const bool hasNormals = mesh.normals.size() == mesh.positions.size();
        for (std::size_t v = 0; v < mesh.positions.size(); ++v)
        {
            std::int64_t& cluster = clusterOf[cellOf(mesh.positions[v])];
            if (cluster < 0)
            {
                cluster = static_cast<std::int64_t>(simple.positions.size());
                simple.positions.emplace_back(0.0f);
                simple.normals.emplace_back(0.0f);
                simple.uv0.emplace_back(0.0f);
                counts.push_back(0);
            }
            const auto at = static_cast<std::size_t>(cluster);
            remap[v] = static_cast<std::uint32_t>(at);
            simple.positions[at] += mesh.positions[v];
            simple.normals[at] += hasNormals ? mesh.normals[v] : Vec3(0.0f);
            simple.uv0[at] += hasUv ? mesh.uv0[v] : Fenrir::Math::Vec2(0.0f);
            ++counts[at];
        }
        for (std::size_t c = 0; c < counts.size(); ++c)
        {
            const auto count = static_cast<float>(counts[c]);
            simple.positions[c] /= count;
            simple.uv0[c] /= count;
            // Opposite normals in one cell (a thin edge) can cancel: the cell then faces out from the centre.
            const Vec3 normal = simple.normals[c];
            const Vec3 outward = simple.positions[c] - (mesh.bounds.min + mesh.bounds.max) * 0.5f;
            simple.normals[c] = glm::length(normal) > 1e-4f    ? glm::normalize(normal)
                                : glm::length(outward) > 1e-6f ? glm::normalize(outward)
                                                               : Vec3(0.0f, 1.0f, 0.0f);
        }
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
        {
            const std::uint32_t a = remap[mesh.indices[i]];
            const std::uint32_t b = remap[mesh.indices[i + 1]];
            const std::uint32_t c = remap[mesh.indices[i + 2]];
            if (a != b && b != c && a != c)
            {
                simple.indices.insert(simple.indices.end(), {a, b, c});
            }
        }
        return simple;
    }
} // namespace LightingDemo
