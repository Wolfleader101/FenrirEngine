#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "FenrirAssets/MeshData.hpp"
#include "FenrirMath/Math.hpp"

// The forest clearing's ground, as functions of a point on it (x and z in metres, the clearing's middle at the origin):
// the terrain's height, how far into the forest and onto the path a point is, and the meshes made from them, the
// ground's and the grass's. The scene places everything on the same functions, so plants stand on the ground and the
// grass and ferns keep off the path.
namespace LightingDemo::Forest
{
    /** @brief The clearing's mean radius, in metres: about 40 metres across. */
    inline constexpr float ClearingRadius = 19.0f;
    /** @brief How far the trees reach from the middle, and the ground beyond them. */
    inline constexpr float ForestReach = 75.0f;
    inline constexpr float GroundHalf = 95.0f;

    /** @brief The clearing's edge, in metres from the middle, at the bearing `angle` (radians about +Y): ragged. */
    [[nodiscard]] float ClearingEdge(float angle);

    /** @brief How far past the clearing's edge `ground` is, in metres; negative inside the clearing. */
    [[nodiscard]] float PastEdge(Fenrir::Math::Vec2 ground);

    /** @brief 0 in the open clearing to 1 under the trees, over the few metres either side of the edge. */
    [[nodiscard]] float ForestWeight(Fenrir::Math::Vec2 ground);

    /** @brief 0 at the clearing's edge to 1 deep in the forest, twenty metres in: how much canopy is overhead. */
    [[nodiscard]] float CanopyDepth(Fenrir::Math::Vec2 ground);

    /** @brief 1 on the dirt path's middle to 0 off it, with a ragged edge; the path winds through the clearing. */
    [[nodiscard]] float PathWeight(Fenrir::Math::Vec2 ground);

    /** @brief The terrain's height at `ground`: a shallow hollow, gently rolling, the path worn a little lower. */
    [[nodiscard]] float Height(Fenrir::Math::Vec2 ground);

    /** @brief The point on the ground under `ground`, at its height. */
    [[nodiscard]] Fenrir::Math::Vec3 OnGround(Fenrir::Math::Vec2 ground);

    /**
     * @brief The ground's mesh: a grid `GroundHalf` metres each way at half a metre, on Height, its normals from the
     * height's slope, UV0 in metres (the ground shader scales each layer's), and each vertex's colour holding its
     * ForestWeight in red and its PathWeight in green, which the ground shader blends its layers by, and its
     * CanopyDepth in blue, which darkens it.
     */
    [[nodiscard]] Fenrir::MeshData GroundMesh();

    /** @brief A circle the grass keeps out of: under the boulder and the log. */
    struct Clear
    {
        Fenrir::Math::Vec2 centre{0.0f};
        float radius = 0.0f;
    };

    /**
     * @brief The grass: about `blades` blades over the clearing in square chunks of 8 metres, one mesh a chunk, thick
     * in the open, thinning to nothing under the trees and on the path, and out of `clear`. Each blade stands on the
     * ground, a tapering, curving strip of three segments and a tip with a face each way (ten triangles), so the
     * material is single-sided and both faces' normals lean up; its height, lean, turn and tint are its own, shorter
     * and drier by the path. UV0's u is the blade's height in metres and v the way up it, 0 to 1, which grass.vert
     * sways by and grass.frag shades along.
     */
    [[nodiscard]] std::vector<Fenrir::MeshData> GrassChunks(std::size_t blades, std::span<const Clear> clear,
                                                            std::uint32_t seed);
} // namespace LightingDemo::Forest
