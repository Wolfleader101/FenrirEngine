#pragma once

#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirPainter/Paint.hpp"
#include "FenrirUI/Style.hpp"

// The shapes an element's style gives its laid-out box, shared by painting and hit testing so a click lands on what was
// drawn.
namespace Fenrir::Detail
{
    /** @brief `length` against `reference` (a percentage's whole); `auto` is 0. */
    [[nodiscard]] float ResolveLength(const Length& length, float reference);

    /** @brief The border box's corner radii. */
    [[nodiscard]] CornerRadii RadiiOf(const ResolvedStyle& style);

    /** @brief The padding box's corners: each outer radius less the wider of the borders that meet there. */
    [[nodiscard]] CornerRadii InnerRadiiOf(const ResolvedStyle& style);

    /** @brief The element's transform about its origin, from its local UI units to its parent's. */
    [[nodiscard]] Transform2D ElementTransform(const ResolvedStyle& style, const Rect2D& border);

    /**
     * @brief Whether `point` is inside `rect` with its corners rounded by `radii`, the radii scaled down together when
     * two on one side add up to more than the side, as CSS does.
     */
    [[nodiscard]] bool ContainsRounded(const Rect2D& rect, const CornerRadii& radii, Math::Vec2 point);
} // namespace Fenrir::Detail
