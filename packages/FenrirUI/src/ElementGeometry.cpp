#include "ElementGeometry.hpp"

#include <algorithm>

namespace Fenrir::Detail
{
    float ResolveLength(const Length& length, float reference)
    {
        switch (length.unit)
        {
        case LengthUnit::Px:
            return length.value;
        case LengthUnit::Percent:
            return length.value / 100.0f * reference;
        case LengthUnit::Auto:
            break;
        }
        return 0.0f;
    }

    CornerRadii RadiiOf(const ResolvedStyle& style)
    {
        return {style.borderTopLeftRadius, style.borderTopRightRadius, style.borderBottomRightRadius,
                style.borderBottomLeftRadius};
    }

    CornerRadii InnerRadiiOf(const ResolvedStyle& style)
    {
        const auto inner = [](float radius, float a, float b) { return std::max(0.0f, radius - std::max(a, b)); };
        return {inner(style.borderTopLeftRadius, style.borderTopWidth, style.borderLeftWidth),
                inner(style.borderTopRightRadius, style.borderTopWidth, style.borderRightWidth),
                inner(style.borderBottomRightRadius, style.borderBottomWidth, style.borderRightWidth),
                inner(style.borderBottomLeftRadius, style.borderBottomWidth, style.borderLeftWidth)};
    }

    Transform2D ElementTransform(const ResolvedStyle& style, const Rect2D& border)
    {
        const Math::Vec2 origin{border.x + ResolveLength(style.transformOriginX, border.width),
                                border.y + ResolveLength(style.transformOriginY, border.height)};
        Transform2D transform = Transform2D::Translation(origin);
        for (const TransformStep& step : style.transform)
        {
            switch (step.kind)
            {
            case TransformStep::Kind::Translate:
                transform = transform * Transform2D::Translation({ResolveLength(step.x, border.width),
                                                                  ResolveLength(step.y, border.height)});
                break;
            case TransformStep::Kind::Scale:
                transform = transform * Transform2D::Scaling(step.factors);
                break;
            case TransformStep::Kind::Rotate:
                transform = transform * Transform2D::Rotation(step.radians);
                break;
            }
        }
        return transform * Transform2D::Translation(-origin);
    }

    bool ContainsRounded(const Rect2D& rect, const CornerRadii& radii, Math::Vec2 point)
    {
        const float x0 = rect.x;
        const float y0 = rect.y;
        const float x1 = rect.x + rect.width;
        const float y1 = rect.y + rect.height;
        if (point.x < x0 || point.y < y0 || point.x >= x1 || point.y >= y1)
        {
            return false;
        }
        // CSS's rule for radii that do not fit: one factor for all four, the smallest any side needs.
        float fit = 1.0f;
        const auto limit = [&](float a, float b, float side) {
            if (a + b > side && a + b > 0.0f)
            {
                fit = std::min(fit, side / (a + b));
            }
        };
        limit(radii.topLeft, radii.topRight, rect.width);
        limit(radii.bottomLeft, radii.bottomRight, rect.width);
        limit(radii.topLeft, radii.bottomLeft, rect.height);
        limit(radii.topRight, radii.bottomRight, rect.height);
        const auto outsideCorner = [&](float radius, float cornerX, float cornerY, float towardX, float towardY) {
            const float r = radius * fit;
            if (r <= 0.0f)
            {
                return false;
            }
            // The arc's centre is `r` in from the corner along both edges.
            const Math::Vec2 centre{cornerX + towardX * r, cornerY + towardY * r};
            const bool inCornerSquare = (point.x - centre.x) * towardX < 0.0f && (point.y - centre.y) * towardY < 0.0f;
            if (!inCornerSquare)
            {
                return false;
            }
            const Math::Vec2 offset = point - centre;
            return offset.x * offset.x + offset.y * offset.y > r * r;
        };
        return !(outsideCorner(radii.topLeft, x0, y0, 1.0f, 1.0f) ||
                 outsideCorner(radii.topRight, x1, y0, -1.0f, 1.0f) ||
                 outsideCorner(radii.bottomRight, x1, y1, -1.0f, -1.0f) ||
                 outsideCorner(radii.bottomLeft, x0, y1, 1.0f, -1.0f));
    }
} // namespace Fenrir::Detail
