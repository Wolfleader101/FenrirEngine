#pragma once

#include <algorithm>

#include "FenrirDraw2D/DrawList2D.hpp"

namespace Fenrir::Detail
{
    /** @brief Where `a` and `b` overlap; an empty rectangle at the nearer corner when they do not. */
    [[nodiscard]] inline Rect2D Intersect(const Rect2D& a, const Rect2D& b)
    {
        const float x0 = std::max(a.x, b.x);
        const float y0 = std::max(a.y, b.y);
        const float x1 = std::max(x0, std::min(a.x + a.width, b.x + b.width));
        const float y1 = std::max(y0, std::min(a.y + a.height, b.y + b.height));
        return {x0, y0, x1 - x0, y1 - y0};
    }

    /** @brief The smallest rectangle holding `a` and `b`. */
    [[nodiscard]] inline Rect2D Union(const Rect2D& a, const Rect2D& b)
    {
        const float x0 = std::min(a.x, b.x);
        const float y0 = std::min(a.y, b.y);
        const float x1 = std::max(a.x + a.width, b.x + b.width);
        const float y1 = std::max(a.y + a.height, b.y + b.height);
        return {x0, y0, x1 - x0, y1 - y0};
    }
} // namespace Fenrir::Detail
