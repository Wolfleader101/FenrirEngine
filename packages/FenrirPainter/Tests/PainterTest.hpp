#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

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

    /** @brief The list's index bytes read as 16-bit indices. */
    inline std::vector<std::uint16_t> Indices16(const Fenrir::DrawList2D& list)
    {
        std::vector<std::uint16_t> indices(list.indices.size() / sizeof(std::uint16_t));
        std::memcpy(indices.data(), list.indices.data(), indices.size() * sizeof(std::uint16_t));
        return indices;
    }

    inline bool Near(float a, float b, float tolerance = 1e-4f)
    {
        return std::abs(a - b) <= tolerance;
    }

    inline bool Near(Fenrir::Math::Vec2 a, Fenrir::Math::Vec2 b, float tolerance = 1e-4f)
    {
        return Near(a.x, b.x, tolerance) && Near(a.y, b.y, tolerance);
    }

    inline bool Near(Fenrir::Math::Vec4 a, Fenrir::Math::Vec4 b, float tolerance = 1e-4f)
    {
        return Near(a.x, b.x, tolerance) && Near(a.y, b.y, tolerance) && Near(a.z, b.z, tolerance) &&
               Near(a.w, b.w, tolerance);
    }

    inline Fenrir::ShapeKind ShapeOf(const Fenrir::PainterVertex& vertex)
    {
        return static_cast<Fenrir::ShapeKind>(vertex.kinds[0]);
    }

    inline Fenrir::PaintKind PaintOf(const Fenrir::PainterVertex& vertex)
    {
        return static_cast<Fenrir::PaintKind>(vertex.kinds[1]);
    }

    inline bool HasFlag(const Fenrir::PainterVertex& vertex, std::uint8_t flag)
    {
        return (vertex.kinds[2] & flag) != 0;
    }
} // namespace PainterTest
