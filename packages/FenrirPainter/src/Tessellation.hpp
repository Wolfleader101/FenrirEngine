#pragma once

#include <cstdint>
#include <vector>

#include "FenrirMath/Math.hpp"
#include "FenrirPainter/Paint.hpp"
#include "FenrirPainter/Path.hpp"

namespace Fenrir::PainterDetail
{
    /** @brief One flattened subpath: its points in order, and whether it closes back to its first. */
    struct Contour
    {
        std::vector<Math::Vec2> points;
        bool closed = false;
    };

    /**
     * @brief `path`'s subpaths under `transform`, each curve split into lines that stray from it by at most
     * `tolerance` (in the transformed space). Consecutive repeated points are dropped.
     */
    [[nodiscard]] std::vector<Contour> Flatten(const Path& path, const Transform2D& transform, float tolerance);

    /**
     * @brief The outline of `contours` stroked with `style` (its width taken as given, zero drawing nothing), round
     * joins and caps flattened to `tolerance`: closed contours, each wound the same way round what it covers, so their
     * non-zero fill is the stroke.
     */
    [[nodiscard]] std::vector<Contour> StrokeContours(const std::vector<Contour>& contours, const StrokeStyle& style,
                                                      float tolerance);

    /**
     * @brief A fill as triangles: each boundary vertex appears twice, moved half the fringe inwards (coverage 1) and
     * half outwards (coverage 0). The solid triangles join the inner vertices; the fringe triangles join each boundary
     * edge's inner and outer pairs. With no fringe, each boundary vertex appears once, where it is (coverage 1), and
     * there are only the solid triangles. Coordinates are those of the contours.
     */
    struct Tessellation
    {
        std::vector<Math::Vec2> positions;
        std::vector<float> coverage;
        std::vector<std::uint32_t> indices;
    };

    /**
     * @brief The region `contours` cover under `rule` (each closed, whatever its flag says): split by winding into
     * boundary loops that cross nowhere, holes bridged into the loops round them, ear-clipped, with a `fringe` wide
     * ring about every loop, or none when `fringe` is not above zero.
     */
    [[nodiscard]] Tessellation Tessellate(const std::vector<Contour>& contours, FillRule rule, float fringe);
} // namespace Fenrir::PainterDetail
