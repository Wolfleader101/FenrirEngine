#include "ForestGround.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <random>
#include <utility>

namespace LightingDemo::Forest
{
    namespace
    {
        using Fenrir::Math::Vec2;
        using Fenrir::Math::Vec3;

        constexpr float Pi = std::numbers::pi_v<float>;
        /** @brief The ground grid's spacing, in metres. */
        constexpr float GroundStep = 0.5f;
        /** @brief The grass's chunks: their size, and how far out from the middle they reach. */
        constexpr float ChunkSize = 8.0f;
        constexpr float GrassReach = 26.0f;

        /**
         * @brief The path's course: in from the forest to the south, past where the camera starts, across the clearing
         * and out to the north-east, towards the sun. A Catmull-Rom curve runs through these points.
         */
        const std::array<Vec2, 7> PathPoints{{{-7.0f, 40.0f},
                                              {-4.5f, 26.0f},
                                              {-3.2f, 16.5f},
                                              {1.5f, 6.0f},
                                              {-1.0f, -5.0f},
                                              {4.5f, -15.0f},
                                              {10.0f, -32.0f}}};
        /** @brief The path's half-width, in metres, and the width of its soft edge. */
        constexpr float PathHalfWidth = 0.75f;
        constexpr float PathEdge = 0.6f;

        float Smoothstep(float edge0, float edge1, float x)
        {
            const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        /** @brief The path's curve as a polyline of short straight pieces, made once. */
        const std::vector<Vec2>& PathPolyline()
        {
            static const std::vector<Vec2> line = [] {
                std::vector<Vec2> points;
                constexpr int Steps = 16;
                for (std::size_t i = 0; i + 1 < PathPoints.size(); ++i)
                {
                    const Vec2 p0 = PathPoints[i == 0 ? 0 : i - 1];
                    const Vec2 p1 = PathPoints[i];
                    const Vec2 p2 = PathPoints[i + 1];
                    const Vec2 p3 = PathPoints[std::min(i + 2, PathPoints.size() - 1)];
                    for (int s = 0; s < Steps; ++s)
                    {
                        const float t = static_cast<float>(s) / Steps;
                        const float t2 = t * t;
                        const float t3 = t2 * t;
                        points.push_back(0.5f *
                                         (2.0f * p1 + (p2 - p0) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                                          (3.0f * p1 - p0 - 3.0f * p2 + p3) * t3));
                    }
                }
                points.push_back(PathPoints.back());
                return points;
            }();
            return line;
        }

        /** @brief How far `ground` is from the path's middle; any distance past a few metres reads as far. */
        float DistanceToPath(Vec2 ground)
        {
            const std::vector<Vec2>& line = PathPolyline();
            // Most of the ground is far from the path: its box, a few metres wider, rules those points out at once.
            static const std::pair<Vec2, Vec2> box = [&] {
                std::pair<Vec2, Vec2> bounds{line.front(), line.front()};
                for (const Vec2 point : line)
                {
                    bounds = {glm::min(bounds.first, point), glm::max(bounds.second, point)};
                }
                return std::pair<Vec2, Vec2>{bounds.first - Vec2(4.0f), bounds.second + Vec2(4.0f)};
            }();
            if (glm::any(glm::lessThan(ground, box.first)) || glm::any(glm::greaterThan(ground, box.second)))
            {
                return 4.0f;
            }
            float nearest = std::numeric_limits<float>::max();
            for (std::size_t i = 0; i + 1 < line.size(); ++i)
            {
                const Vec2 along = line[i + 1] - line[i];
                const float t = std::clamp(glm::dot(ground - line[i], along) / glm::dot(along, along), 0.0f, 1.0f);
                nearest = std::min(nearest, glm::distance(ground, line[i] + along * t));
            }
            return nearest;
        }

        /** @brief A smooth, cheap wobble in -1..1 across the ground, a few metres a wave. */
        float Wobble(Vec2 ground, float scale)
        {
            return 0.5f * (std::sin(ground.x * 1.7f * scale + 0.3f) * std::cos(ground.y * 1.3f * scale - 1.1f) +
                           std::sin((ground.x + ground.y) * 2.3f * scale + 2.0f));
        }
    } // namespace

    float ClearingEdge(float angle)
    {
        return ClearingRadius + 2.5f * std::sin(3.0f * angle + 1.0f) + 1.5f * std::sin(5.0f * angle + 2.0f) +
               1.0f * std::sin(7.0f * angle);
    }

    float PastEdge(Vec2 ground)
    {
        return glm::length(ground) - ClearingEdge(std::atan2(ground.y, ground.x));
    }

    float ForestWeight(Vec2 ground)
    {
        return Smoothstep(-2.5f, 3.5f, PastEdge(ground) + 1.2f * Wobble(ground, 0.4f));
    }

    float CanopyDepth(Vec2 ground)
    {
        return Smoothstep(0.0f, 20.0f, PastEdge(ground));
    }

    float PathWeight(Vec2 ground)
    {
        const float distance = DistanceToPath(ground) + 0.18f * Wobble(ground, 1.5f);
        // The path fades as it enters the forest, a trodden line among the trees.
        const float width = PathHalfWidth * (1.0f - 0.35f * ForestWeight(ground));
        return 1.0f - Smoothstep(width, width + PathEdge, distance);
    }

    float Height(Vec2 ground)
    {
        const float rolling = 0.35f * std::sin(0.11f * ground.x + 0.4f) * std::cos(0.09f * ground.y) +
                              0.2f * std::sin(0.23f * ground.x - 0.17f * ground.y);
        // The clearing lies in a shallow hollow, the ground rising into the forest.
        const float rise = 1.2f * Smoothstep(0.0f, 25.0f, PastEdge(ground));
        return rolling + rise - 0.06f * PathWeight(ground);
    }

    Vec3 OnGround(Vec2 ground)
    {
        return {ground.x, Height(ground), ground.y};
    }

    Fenrir::MeshData GroundMesh()
    {
        const int cells = static_cast<int>(2.0f * GroundHalf / GroundStep);
        const int side = cells + 1;
        Fenrir::MeshData mesh;
        mesh.positions.reserve(static_cast<std::size_t>(side) * side);
        for (int row = 0; row < side; ++row)
        {
            for (int column = 0; column < side; ++column)
            {
                const Vec2 ground{-GroundHalf + static_cast<float>(column) * GroundStep,
                                  -GroundHalf + static_cast<float>(row) * GroundStep};
                mesh.positions.push_back(OnGround(ground));
                // The slope by central differences.
                const float dx = Height(ground + Vec2(0.1f, 0.0f)) - Height(ground - Vec2(0.1f, 0.0f));
                const float dz = Height(ground + Vec2(0.0f, 0.1f)) - Height(ground - Vec2(0.0f, 0.1f));
                mesh.normals.push_back(glm::normalize(Vec3(-dx, 0.2f, -dz)));
                // u along +X, v along +Z: the maps' green (up the image, towards -v) is -Z, as cross(N, T) gives.
                mesh.tangents.emplace_back(1.0f, 0.0f, 0.0f, 1.0f);
                mesh.uv0.push_back(ground);
                mesh.colours.push_back({ForestWeight(ground), PathWeight(ground), CanopyDepth(ground), 1.0f});
            }
        }
        // Counter-clockwise seen from above.
        for (int row = 0; row < cells; ++row)
        {
            for (int column = 0; column < cells; ++column)
            {
                const auto at = [&](int r, int c) { return static_cast<std::uint32_t>(r * side + c); };
                mesh.indices.insert(mesh.indices.end(),
                                    {at(row, column), at(row + 1, column), at(row + 1, column + 1), at(row, column),
                                     at(row + 1, column + 1), at(row, column + 1)});
            }
        }
        return mesh;
    }

    std::vector<Fenrir::MeshData> GrassChunks(std::size_t blades, std::span<const Clear> clear, std::uint32_t seed)
    {
        std::mt19937 random(seed);
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        // How thick the grass grows at a point, 0 to 1: open clearing, off the path, in clumps.
        const auto density = [&](Vec2 ground) {
            for (const Clear& circle : clear)
            {
                if (glm::distance(ground, circle.centre) < circle.radius)
                {
                    return 0.0f;
                }
            }
            const float open = 1.0f - ForestWeight(ground);
            const float clumps = 0.65f + 0.35f * Wobble(ground, 0.8f);
            return open * open * clumps * (1.0f - PathWeight(ground));
        };
        // Candidates spread evenly over the chunks, each kept with the chance of its density; their number is chosen
        // from a coarse survey of the density so that about `blades` are kept.
        const int chunks = static_cast<int>(std::ceil(GrassReach / ChunkSize));
        float total = 0.0f;
        for (float x = -GrassReach; x < GrassReach; x += 1.0f)
        {
            for (float z = -GrassReach; z < GrassReach; z += 1.0f)
            {
                total += density({x + 0.5f, z + 0.5f});
            }
        }
        const float perSquareMetre = static_cast<float>(blades) / std::max(total, 1.0f);

        std::vector<Fenrir::MeshData> meshes;
        constexpr int Segments = 3;
        const Vec3 up{0.0f, 1.0f, 0.0f};
        for (int cx = -chunks; cx < chunks; ++cx)
        {
            for (int cz = -chunks; cz < chunks; ++cz)
            {
                const Vec2 corner{static_cast<float>(cx) * ChunkSize, static_cast<float>(cz) * ChunkSize};
                const auto candidates = static_cast<int>(perSquareMetre * ChunkSize * ChunkSize);
                Fenrir::MeshData mesh;
                for (int i = 0; i < candidates; ++i)
                {
                    const Vec2 ground = corner + Vec2(unit(random), unit(random)) * ChunkSize;
                    const float thick = density(ground);
                    if (unit(random) >= thick)
                    {
                        continue;
                    }
                    const float trodden = PathWeight(ground + Vec2(0.8f, 0.0f)) + PathWeight(ground - Vec2(0.8f, 0.0f));
                    // Mostly knee-low, a few taller stems; shorter where it is thin and beside the path.
                    const float height = (0.25f + 0.55f * std::pow(unit(random), 1.4f)) * (0.55f + 0.45f * thick) *
                                         (1.0f - 0.3f * trodden);
                    const float width = 0.012f + 0.016f * unit(random);
                    const float turn = 2.0f * Pi * unit(random);
                    const Vec3 side{std::cos(turn), 0.0f, std::sin(turn)};
                    const float leanTurn = 2.0f * Pi * unit(random);
                    const Vec3 lean =
                        Vec3(std::cos(leanTurn), 0.0f, std::sin(leanTurn)) * (0.1f + 0.35f * unit(random));
                    const Vec3 facing = glm::cross(side, up);
                    // Mostly green in a range of shades; dry and straw-coloured more often by the path.
                    const float shade = 0.6f + 0.4f * unit(random);
                    const bool dry = unit(random) < 0.08f + 0.3f * std::min(trodden, 1.0f);
                    const Fenrir::Colour tint = dry ? Fenrir::Colour{shade, shade * 0.85f, shade * 0.5f, 1.0f}
                                                    : Fenrir::Colour{shade * 0.85f, shade, shade * 0.75f, 1.0f};
                    const Vec3 root = OnGround(ground);
                    for (int face = 0; face < 2; ++face)
                    {
                        // Each face's normal leans up from its own side.
                        const Vec3 normal = glm::normalize((face == 0 ? facing : -facing) + up * 0.7f);
                        const auto first = static_cast<std::uint32_t>(mesh.positions.size());
                        const auto add = [&](Vec3 position, float t) {
                            mesh.positions.push_back(position);
                            mesh.normals.push_back(normal);
                            mesh.uv0.emplace_back(height, t);
                            mesh.colours.push_back(tint);
                        };
                        for (int s = 0; s <= Segments; ++s)
                        {
                            const float t = static_cast<float>(s) / Segments;
                            // Curving over as it rises: lean by the square of the way up.
                            const Vec3 centre = root + lean * height * t * t + up * (height * t);
                            if (s == Segments)
                            {
                                add(centre, 1.0f);
                                break;
                            }
                            const float half = 0.5f * width * (1.0f - 0.7f * t);
                            add(centre - side * half, t);
                            add(centre + side * half, t);
                        }
                        // Counter-clockwise seen from the face's side.
                        const auto triangle = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
                            if (face == 0)
                            {
                                mesh.indices.insert(mesh.indices.end(), {a, b, c});
                            }
                            else
                            {
                                mesh.indices.insert(mesh.indices.end(), {a, c, b});
                            }
                        };
                        for (std::uint32_t s = 0; s < Segments - 1; ++s)
                        {
                            const std::uint32_t left = first + 2 * s;
                            triangle(left, left + 1, left + 2);
                            triangle(left + 1, left + 3, left + 2);
                        }
                        const std::uint32_t last = first + 2 * (Segments - 1);
                        triangle(last, last + 1, last + 2);
                    }
                }
                if (!mesh.positions.empty())
                {
                    meshes.push_back(std::move(mesh));
                }
            }
        }
        return meshes;
    }
} // namespace LightingDemo::Forest
