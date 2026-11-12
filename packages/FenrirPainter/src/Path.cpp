#include "FenrirPainter/Path.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "Radii.hpp"
#include "Tessellation.hpp"

namespace Fenrir
{
    namespace
    {
        constexpr float HalfPi = 0.5f * std::numbers::pi_v<float>;

        Math::Vec2 OnCircle(float angle)
        {
            return {std::cos(angle), std::sin(angle)};
        }

        float Cross(Math::Vec2 a, Math::Vec2 b)
        {
            return a.x * b.y - a.y * b.x;
        }

        float Length(Math::Vec2 v)
        {
            return std::sqrt(v.x * v.x + v.y * v.y);
        }
    } // namespace

    void Path::Edited()
    {
        m_shape.reset();
    }

    void Path::EnsureStarted(Math::Vec2 point)
    {
        if (m_closed)
        {
            // After Close the next step starts where the closed subpath did.
            m_verbs.push_back(PathVerb::Move);
            m_points.push_back(m_start);
            m_current = m_start;
            m_open = true;
            m_closed = false;
        }
        if (!m_open)
        {
            m_verbs.push_back(PathVerb::Move);
            m_points.push_back(point);
            m_start = point;
            m_current = point;
            m_open = true;
        }
    }

    Path& Path::MoveTo(Math::Vec2 point)
    {
        Edited();
        m_verbs.push_back(PathVerb::Move);
        m_points.push_back(point);
        m_start = point;
        m_current = point;
        m_open = true;
        m_closed = false;
        return *this;
    }

    Path& Path::LineTo(Math::Vec2 point)
    {
        Edited();
        EnsureStarted(point);
        m_verbs.push_back(PathVerb::Line);
        m_points.push_back(point);
        m_current = point;
        return *this;
    }

    Path& Path::QuadTo(Math::Vec2 control, Math::Vec2 end)
    {
        Edited();
        EnsureStarted(control);
        m_verbs.push_back(PathVerb::Quad);
        m_points.push_back(control);
        m_points.push_back(end);
        m_current = end;
        return *this;
    }

    Path& Path::CubicTo(Math::Vec2 control1, Math::Vec2 control2, Math::Vec2 end)
    {
        Edited();
        EnsureStarted(control1);
        m_verbs.push_back(PathVerb::Cubic);
        m_points.push_back(control1);
        m_points.push_back(control2);
        m_points.push_back(end);
        m_current = end;
        return *this;
    }

    Path& Path::Arc(Math::Vec2 centre, float radius, float startRadians, float sweepRadians)
    {
        Edited();
        const Math::Vec2 start = centre + OnCircle(startRadians) * radius;
        if (m_open && !m_closed)
        {
            LineTo(start);
        }
        else
        {
            MoveTo(start);
        }
        if (radius <= 0.0f || sweepRadians == 0.0f)
        {
            return *this;
        }
        // Each piece of at most a quarter turn is one cubic, its control points along the tangents at 4/3 tan(t / 4).
        const int pieces = std::max(1, static_cast<int>(std::ceil(std::abs(sweepRadians) / HalfPi - 1e-4f)));
        const float step = sweepRadians / static_cast<float>(pieces);
        const float k = 4.0f / 3.0f * std::tan(step / 4.0f) * radius;
        float angle = startRadians;
        for (int i = 0; i < pieces; ++i)
        {
            const float next = angle + step;
            const Math::Vec2 from = centre + OnCircle(angle) * radius;
            const Math::Vec2 to = centre + OnCircle(next) * radius;
            const Math::Vec2 fromTangent{-std::sin(angle), std::cos(angle)};
            const Math::Vec2 toTangent{-std::sin(next), std::cos(next)};
            m_verbs.push_back(PathVerb::Cubic);
            m_points.push_back(from + fromTangent * k);
            m_points.push_back(to - toTangent * k);
            m_points.push_back(to);
            angle = next;
        }
        m_current = m_points.back();
        return *this;
    }

    Path& Path::ArcTo(Math::Vec2 corner, Math::Vec2 towards, float radius)
    {
        Edited();
        if (!m_open || m_closed)
        {
            EnsureStarted(corner);
        }
        const Math::Vec2 from = m_current;
        const Math::Vec2 in = from - corner;
        const Math::Vec2 out = towards - corner;
        const float inLength = Length(in);
        const float outLength = Length(out);
        if (radius <= 0.0f || inLength <= 0.0f || outLength <= 0.0f ||
            std::abs(Cross(in, out)) < 1e-6f * inLength * outLength)
        {
            return LineTo(corner);
        }
        const Math::Vec2 u = in / inLength;
        const Math::Vec2 v = out / outLength;
        // The half angle between the lines; the tangent points lie radius / tan(half) from the corner.
        const float cosine = std::clamp(u.x * v.x + u.y * v.y, -1.0f, 1.0f);
        const float half = 0.5f * std::acos(cosine);
        const float reach = radius / std::tan(half);
        const Math::Vec2 bisector = Math::Normalized(u + v);
        const Math::Vec2 centre = corner + bisector * (radius / std::sin(half));
        const Math::Vec2 first = corner + u * reach;
        const Math::Vec2 last = corner + v * reach;
        const float startAngle = std::atan2(first.y - centre.y, first.x - centre.x);
        float sweep = std::atan2(last.y - centre.y, last.x - centre.x) - startAngle;
        // The arc is the short way round, less than half a turn.
        if (sweep > std::numbers::pi_v<float>)
        {
            sweep -= 2.0f * std::numbers::pi_v<float>;
        }
        else if (sweep < -std::numbers::pi_v<float>)
        {
            sweep += 2.0f * std::numbers::pi_v<float>;
        }
        return Arc(centre, radius, startAngle, sweep);
    }

    Path& Path::Close()
    {
        Edited();
        if (m_open && !m_closed)
        {
            m_verbs.push_back(PathVerb::Close);
            m_current = m_start;
            m_closed = true;
        }
        return *this;
    }

    Path& Path::AddRect(const Rect2D& rect)
    {
        return AddRoundedRect(rect, {});
    }

    Path& Path::AddRoundedRect(const Rect2D& rect, const CornerRadii& radii)
    {
        const bool alone = m_verbs.empty();
        const Math::Vec4 r = PainterDetail::FitRadii(rect, radii);
        const float x0 = rect.x;
        const float y0 = rect.y;
        const float x1 = rect.x + rect.width;
        const float y1 = rect.y + rect.height;
        // Clockwise on screen from the top edge, each rounded corner a quarter turn.
        MoveTo({x0 + r.x, y0});
        LineTo({x1 - r.y, y0});
        if (r.y > 0.0f)
        {
            Arc({x1 - r.y, y0 + r.y}, r.y, -HalfPi, HalfPi);
        }
        LineTo({x1, y1 - r.z});
        if (r.z > 0.0f)
        {
            Arc({x1 - r.z, y1 - r.z}, r.z, 0.0f, HalfPi);
        }
        LineTo({x0 + r.w, y1});
        if (r.w > 0.0f)
        {
            Arc({x0 + r.w, y1 - r.w}, r.w, HalfPi, HalfPi);
        }
        LineTo({x0, y0 + r.x});
        if (r.x > 0.0f)
        {
            Arc({x0 + r.x, y0 + r.x}, r.x, std::numbers::pi_v<float>, HalfPi);
        }
        Close();
        if (alone)
        {
            m_shape = Shape{Shape::Kind::RoundedRect, rect, {r.x, r.y, r.z, r.w}};
        }
        return *this;
    }

    Path& Path::AddCircle(Math::Vec2 centre, float radius)
    {
        const bool alone = m_verbs.empty();
        AddEllipse(centre, {radius, radius});
        if (alone && radius > 0.0f)
        {
            m_shape =
                Shape{Shape::Kind::Circle, {centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f}, {}};
        }
        return *this;
    }

    Path& Path::AddEllipse(Math::Vec2 centre, Math::Vec2 radii)
    {
        // Four quarter turns, each a cubic whose controls lie 4/3 tan(pi / 8) of the radius along the tangent.
        constexpr float K = 0.55228475f;
        const float rx = radii.x;
        const float ry = radii.y;
        const float cx = centre.x;
        const float cy = centre.y;
        MoveTo({cx + rx, cy});
        CubicTo({cx + rx, cy + ry * K}, {cx + rx * K, cy + ry}, {cx, cy + ry});
        CubicTo({cx - rx * K, cy + ry}, {cx - rx, cy + ry * K}, {cx - rx, cy});
        CubicTo({cx - rx, cy - ry * K}, {cx - rx * K, cy - ry}, {cx, cy - ry});
        CubicTo({cx + rx * K, cy - ry}, {cx + rx, cy - ry * K}, {cx + rx, cy});
        return Close();
    }

    Path& Path::AddPolygon(std::span<const Math::Vec2> points)
    {
        AddPolyline(points);
        return points.empty() ? *this : Close();
    }

    Path& Path::AddPolyline(std::span<const Math::Vec2> points)
    {
        Edited();
        if (points.empty())
        {
            return *this;
        }
        MoveTo(points.front());
        for (std::size_t i = 1; i < points.size(); ++i)
        {
            LineTo(points[i]);
        }
        return *this;
    }

    Path Path::Transformed(const Transform2D& transform) const
    {
        Path result = *this;
        for (Math::Vec2& point : result.m_points)
        {
            point = transform.Apply(point);
        }
        result.m_start = transform.Apply(m_start);
        result.m_current = transform.Apply(m_current);
        const bool moveOnly = transform.x == Math::Vec2(1.0f, 0.0f) && transform.y == Math::Vec2(0.0f, 1.0f);
        if (result.m_shape && moveOnly)
        {
            result.m_shape->rect.x += transform.origin.x;
            result.m_shape->rect.y += transform.origin.y;
        }
        else
        {
            // Under any other transform a circle may be an ellipse and a rectangle turned: not the analytic shapes.
            result.m_shape.reset();
        }
        return result;
    }

    Path Path::StrokeOutline(const StrokeStyle& style, float tolerance) const
    {
        StrokeStyle stroke = style;
        if (stroke.width <= 0.0f)
        {
            stroke.width = 1.0f;
        }
        const std::vector<PainterDetail::Contour> outline =
            PainterDetail::StrokeContours(PainterDetail::Flatten(*this, {}, tolerance), stroke, tolerance);
        Path result;
        for (const PainterDetail::Contour& contour : outline)
        {
            result.AddPolygon(contour.points);
        }
        return result;
    }
} // namespace Fenrir
