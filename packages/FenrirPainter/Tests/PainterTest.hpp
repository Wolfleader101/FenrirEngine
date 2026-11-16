#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirPainter/Painter.hpp"
#include "FenrirPainter/PainterVertex.hpp"

namespace PainterTest
{
    /** @brief A stand-in for `builtin:painter`'s handle: the draw-list tests reach no asset store. */
    inline constexpr Fenrir::AssetHandle<Fenrir::Shader> Shader{7, 1};

    /** @brief A painter configuration on the stand-in shader. */
    inline Fenrir::PainterConfig Config()
    {
        Fenrir::PainterConfig config;
        config.shader = Shader;
        return config;
    }

    /** @brief An RGBA8 vertex colour, red in the lowest byte. */
    constexpr std::uint32_t Rgba8(std::uint32_t r, std::uint32_t g, std::uint32_t b, std::uint32_t a)
    {
        return r | (g << 8u) | (b << 16u) | (a << 24u);
    }

    /** @brief The list's vertex bytes read as painter vertices, from vertex `first` (counted at its stride). */
    inline std::vector<Fenrir::PainterVertex> Vertices(const Fenrir::DrawList2D& list, std::size_t first = 0)
    {
        const std::size_t count = list.vertices.size() / sizeof(Fenrir::PainterVertex);
        std::vector<Fenrir::PainterVertex> vertices(count - first);
        std::memcpy(vertices.data(), list.vertices.data() + first * sizeof(Fenrir::PainterVertex),
                    vertices.size() * sizeof(Fenrir::PainterVertex));
        return vertices;
    }

    /** @brief The list's command `index`, which the painter only ever appends as a draw. */
    inline const Fenrir::DrawCommand2D& Draw(const Fenrir::DrawList2D& list, std::size_t index)
    {
        INFO("command " << index << " is a draw");
        REQUIRE(index < list.commands.size());
        REQUIRE(std::holds_alternative<Fenrir::DrawCommand2D>(list.commands[index]));
        return std::get<Fenrir::DrawCommand2D>(list.commands[index]);
    }

    /** @brief The list's index bytes read as 16-bit indices. */
    inline std::vector<std::uint16_t> Indices16(const Fenrir::DrawList2D& list)
    {
        std::vector<std::uint16_t> indices(list.indices.size() / sizeof(std::uint16_t));
        std::memcpy(indices.data(), list.indices.data(), indices.size() * sizeof(std::uint16_t));
        return indices;
    }

    /** @brief Whether `a` is within `tolerance` of `b`. */
    inline bool Near(float a, float b, float tolerance = 1e-4f)
    {
        return std::abs(a - b) <= tolerance;
    }

    /** @brief Whether each component of `a` is within `tolerance` of `b`'s. */
    inline bool Near(Fenrir::Math::Vec2 a, Fenrir::Math::Vec2 b, float tolerance = 1e-4f)
    {
        return Near(a.x, b.x, tolerance) && Near(a.y, b.y, tolerance);
    }

    /** @brief Whether each component of `a` is within `tolerance` of `b`'s. */
    inline bool Near(Fenrir::Math::Vec4 a, Fenrir::Math::Vec4 b, float tolerance = 1e-4f)
    {
        return Near(a.x, b.x, tolerance) && Near(a.y, b.y, tolerance) && Near(a.z, b.z, tolerance) &&
               Near(a.w, b.w, tolerance);
    }

    /** @brief The vertex's shape kind. */
    inline Fenrir::ShapeKind ShapeOf(const Fenrir::PainterVertex& vertex)
    {
        return static_cast<Fenrir::ShapeKind>(vertex.kinds[0]);
    }

    /** @brief The vertex's paint kind. */
    inline Fenrir::PaintKind PaintOf(const Fenrir::PainterVertex& vertex)
    {
        return static_cast<Fenrir::PaintKind>(vertex.kinds[1]);
    }

    /** @brief Whether the vertex has the PainterFlags bit `flag`. */
    inline bool HasFlag(const Fenrir::PainterVertex& vertex, std::uint8_t flag)
    {
        return (vertex.kinds[2] & flag) != 0;
    }

    /** @brief One triangle of a list, its corners' positions and coverages (a triangle vertex's coverage is local.x).
     */
    struct Triangle
    {
        std::array<Fenrir::Math::Vec2, 3> corners;
        std::array<float, 3> coverage;

        /** @brief Every corner fully covered: a triangle of the fill, not of its fringe. */
        [[nodiscard]] bool IsSolid() const
        {
            return coverage[0] == 1.0f && coverage[1] == 1.0f && coverage[2] == 1.0f;
        }

        /** @brief Twice the signed area. */
        [[nodiscard]] float Area() const
        {
            const Fenrir::Math::Vec2 u = corners[1] - corners[0];
            const Fenrir::Math::Vec2 v = corners[2] - corners[0];
            return 0.5f * std::abs(u.x * v.y - u.y * v.x);
        }

        /** @brief Whether `point` lies inside or on it. */
        [[nodiscard]] bool Contains(Fenrir::Math::Vec2 point) const
        {
            const auto side = [&](std::size_t i) {
                const Fenrir::Math::Vec2 a = corners[i];
                const Fenrir::Math::Vec2 b = corners[(i + 1) % 3];
                return (b.x - a.x) * (point.y - a.y) - (b.y - a.y) * (point.x - a.x);
            };
            const float s0 = side(0);
            const float s1 = side(1);
            const float s2 = side(2);
            return (s0 >= 0.0f && s1 >= 0.0f && s2 >= 0.0f) || (s0 <= 0.0f && s1 <= 0.0f && s2 <= 0.0f);
        }
    };

    /** @brief Every triangle of the list's draws of shape kind Triangle, command by command, read through 16-bit
     * indices. */
    inline std::vector<Triangle> Triangles(const Fenrir::DrawList2D& list)
    {
        const std::vector<Fenrir::PainterVertex> vertices = Vertices(list);
        const std::vector<std::uint16_t> indices = Indices16(list);
        std::vector<Triangle> triangles;
        for (const Fenrir::Command2D& entry : list.commands)
        {
            const auto* command = std::get_if<Fenrir::DrawCommand2D>(&entry);
            if (command == nullptr)
            {
                continue;
            }
            for (std::uint32_t i = 0; i + 2 < command->indexCount; i += 3)
            {
                Triangle triangle{};
                bool isPath = true;
                for (std::size_t k = 0; k < 3; ++k)
                {
                    const Fenrir::PainterVertex& vertex =
                        vertices[command->baseVertex + indices[command->indexOffset + i + k]];
                    isPath = isPath && ShapeOf(vertex) == Fenrir::ShapeKind::Triangle;
                    triangle.corners[k] = vertex.position;
                    triangle.coverage[k] = vertex.local.x;
                }
                if (isPath)
                {
                    triangles.push_back(triangle);
                }
            }
        }
        return triangles;
    }

    /** @brief How many of `triangles` are solid (`solid`) or fringe (`!solid`). */
    inline std::size_t Count(const std::vector<Triangle>& triangles, bool solid)
    {
        std::size_t count = 0;
        for (const Triangle& triangle : triangles)
        {
            count += triangle.IsSolid() == solid ? 1 : 0;
        }
        return count;
    }

    /** @brief Whether a solid triangle covers `point`. */
    inline bool Covered(const std::vector<Triangle>& triangles, Fenrir::Math::Vec2 point)
    {
        for (const Triangle& triangle : triangles)
        {
            if (triangle.IsSolid() && triangle.Contains(point))
            {
                return true;
            }
        }
        return false;
    }

    /** @brief The summed area of the solid triangles. */
    inline float SolidArea(const std::vector<Triangle>& triangles)
    {
        float area = 0.0f;
        for (const Triangle& triangle : triangles)
        {
            area += triangle.IsSolid() ? triangle.Area() : 0.0f;
        }
        return area;
    }
} // namespace PainterTest
