#include "MeshTools.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
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

    Fenrir::MeshData ThinnedCards(const Fenrir::MeshData& mesh, int keepEvery, float grow)
    {
        using Fenrir::Math::Vec3;
        const std::size_t vertices = mesh.positions.size();
        // Union-find over the vertices: two vertices of one triangle are in one piece.
        std::vector<std::uint32_t> parent(vertices);
        std::iota(parent.begin(), parent.end(), 0u);
        const auto find = [&](std::uint32_t v) {
            while (parent[v] != v)
            {
                parent[v] = parent[parent[v]];
                v = parent[v];
            }
            return v;
        };
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
        {
            const std::uint32_t a = find(mesh.indices[i]);
            for (std::size_t corner = 1; corner < 3; ++corner)
            {
                const std::uint32_t other = find(mesh.indices[i + corner]);
                if (other != a)
                {
                    parent[other] = a;
                }
            }
        }
        // Pieces numbered in the order their first triangle comes, so every keepEvery-th is spread through the crown.
        constexpr std::uint32_t Unnumbered = ~0u;
        std::vector<std::uint32_t> pieceOf(vertices, Unnumbered);
        std::uint32_t pieces = 0;
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
        {
            std::uint32_t& piece = pieceOf[find(mesh.indices[i])];
            if (piece == Unnumbered)
            {
                piece = pieces++;
            }
        }
        const auto keepPiece = [&](std::uint32_t root) {
            return pieceOf[root] % static_cast<std::uint32_t>(std::max(keepEvery, 1)) == 0;
        };
        // Each kept piece's centre: the mean of its vertices.
        std::vector<Vec3> centres(pieces, Vec3(0.0f));
        std::vector<std::uint32_t> counts(pieces, 0);
        for (std::uint32_t v = 0; v < vertices; ++v)
        {
            const std::uint32_t piece = pieceOf[find(v)];
            if (piece != Unnumbered)
            {
                centres[piece] += mesh.positions[v];
                ++counts[piece];
            }
        }
        // The kept pieces' vertices, renumbered densely, each stream copied.
        std::vector<std::uint32_t> remap(vertices, Unnumbered);
        Fenrir::MeshData thinned;
        const auto copyStream = [](const auto& from, auto& to, std::uint32_t v) {
            if (from.size() > v)
            {
                to.push_back(from[v]);
            }
        };
        for (std::uint32_t v = 0; v < vertices; ++v)
        {
            const std::uint32_t root = find(v);
            if (pieceOf[root] == Unnumbered || !keepPiece(root))
            {
                continue;
            }
            const std::uint32_t piece = pieceOf[root];
            const Vec3 centre = centres[piece] / static_cast<float>(counts[piece]);
            remap[v] = static_cast<std::uint32_t>(thinned.positions.size());
            thinned.positions.push_back(centre + (mesh.positions[v] - centre) * grow);
            copyStream(mesh.normals, thinned.normals, v);
            copyStream(mesh.tangents, thinned.tangents, v);
            copyStream(mesh.uv0, thinned.uv0, v);
            copyStream(mesh.uv1, thinned.uv1, v);
            copyStream(mesh.colours, thinned.colours, v);
        }
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
        {
            if (remap[mesh.indices[i]] != Unnumbered)
            {
                thinned.indices.insert(thinned.indices.end(), {remap[mesh.indices[i]], remap[mesh.indices[i + 1]],
                                                               remap[mesh.indices[i + 2]]});
            }
        }
        return thinned;
    }

    Fenrir::InstanceListData FirstCopies(const Fenrir::InstanceListData& list, std::size_t count)
    {
        Fenrir::InstanceListData cut;
        count = std::min(count, list.transforms.size());
        cut.transforms.assign(list.transforms.begin(), list.transforms.begin() + static_cast<std::ptrdiff_t>(count));
        if (!list.colours.empty())
        {
            cut.colours.assign(list.colours.begin(), list.colours.begin() + static_cast<std::ptrdiff_t>(count));
        }
        return cut;
    }

    Fenrir::Math::Mat4 Placement(Fenrir::Math::Vec3 position, Fenrir::Math::Quat rotation, Fenrir::Math::Vec3 scale)
    {
        return glm::scale(Fenrir::Math::Translate(Fenrir::Math::Mat4(1.0f), position) * glm::mat4_cast(rotation),
                          scale);
    }
} // namespace LightingDemo
