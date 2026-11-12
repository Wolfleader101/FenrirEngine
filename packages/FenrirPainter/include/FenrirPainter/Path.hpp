#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirPainter/Paint.hpp"

namespace Fenrir
{
    /** @brief Which points a path's fill covers: those it winds round at all, or those it winds round an odd number of
     * times. */
    enum class FillRule : std::uint8_t
    {
        NonZero,
        EvenOdd,
    };

    /** @brief One step of a path: each takes its points from the path's point list in order. */
    enum class PathVerb : std::uint8_t
    {
        /** @brief Starts a subpath at one point. */
        Move,
        /** @brief A straight line to one point. */
        Line,
        /** @brief A quadratic Bezier curve through a control point to an end point. */
        Quad,
        /** @brief A cubic Bezier curve through two control points to an end point. */
        Cubic,
        /** @brief A straight line back to the subpath's start, joined to it. Takes no point. */
        Close,
    };

    /** @brief How the open ends of a stroke are drawn. */
    enum class LineCap : std::uint8_t
    {
        /** @brief Flat, at the end point. */
        Butt,
        /** @brief A half disc about the end point. */
        Round,
        /** @brief Flat, half the width past the end point. */
        Square,
    };

    /** @brief How a stroke turns a corner, on its outer side. */
    enum class LineJoin : std::uint8_t
    {
        /** @brief The outer edges extended to meet, or a bevel where they would reach past the miter limit. */
        Miter,
        /** @brief An arc about the corner. */
        Round,
        /** @brief A straight edge across the corner. */
        Bevel,
    };

    /**
     * @brief How a path is stroked: its width in UI units (zero is a hairline, one pixel wide whatever the transform),
     * its caps and joins, and dashes. `dashes` alternates on and off lengths from the start of each subpath, an odd
     * count repeated to make it even, beginning `dashOffset` into the pattern; empty, or with no positive length, it
     * is a solid line.
     */
    struct StrokeStyle
    {
        float width = 1.0f;
        LineCap cap = LineCap::Butt;
        LineJoin join = LineJoin::Miter;
        /** @brief The longest miter, as a multiple of the width, before a corner is bevelled (SVG's limit). */
        float miterLimit = 4.0f;
        std::vector<float> dashes;
        float dashOffset = 0.0f;
    };

    /**
     * @brief A path: subpaths of lines and Bezier curves, in UI units, with the fill rule its fills use. It is a value
     * the caller may keep across frames, copy and transform; the painter flattens it again on each draw, to the
     * tolerance of the transform it is drawn under. Arcs are kept as cubic curves.
     *
     * A drawing step with no current point starts a subpath where it begins; after Close the next step starts a new
     * subpath at the closed one's start.
     */
    class Path
    {
      public:
        /** @brief Starts a subpath at `point`. */
        Path& MoveTo(Math::Vec2 point);
        /** @brief A line to `point`. */
        Path& LineTo(Math::Vec2 point);
        /** @brief A quadratic curve through `control` to `end`. */
        Path& QuadTo(Math::Vec2 control, Math::Vec2 end);
        /** @brief A cubic curve through `control1` and `control2` to `end`. */
        Path& CubicTo(Math::Vec2 control1, Math::Vec2 control2, Math::Vec2 end);
        /**
         * @brief The arc of the circle of `radius` about `centre` from `startRadians` clockwise through `sweepRadians`
         * (negative sweeps go anticlockwise), with a line to its start from the current point if there is one.
         */
        Path& Arc(Math::Vec2 centre, float radius, float startRadians, float sweepRadians);
        /**
         * @brief The browser's arcTo: a line from the current point towards `corner` and an arc of `radius` round it,
         * tangent to the lines from the current point to `corner` and from `corner` to `towards`, ending on the second
         * line. With no current point it moves to `corner`; with the three points in a line, or no radius, it is a
         * line to `corner`.
         */
        Path& ArcTo(Math::Vec2 corner, Math::Vec2 towards, float radius);
        /** @brief Closes the current subpath with a line back to its start. */
        Path& Close();

        /** @name Helpers
         * Each adds closed subpaths, or for a polyline an open one, wound clockwise on screen.
         * @{
         */
        /** @brief The rectangle `rect`. */
        Path& AddRect(const Rect2D& rect);
        /** @brief `rect` with corners rounded by `radii`, scaled down together when they do not fit, as CSS does. */
        Path& AddRoundedRect(const Rect2D& rect, const CornerRadii& radii);
        /** @brief The circle of `radius` about `centre`. */
        Path& AddCircle(Math::Vec2 centre, float radius);
        /** @brief The ellipse of `radii` (half its width and height) about `centre`. */
        Path& AddEllipse(Math::Vec2 centre, Math::Vec2 radii);
        /** @brief The closed polygon through `points`. */
        Path& AddPolygon(std::span<const Math::Vec2> points);
        /** @brief The open polyline through `points`. */
        Path& AddPolyline(std::span<const Math::Vec2> points);
        /** @} */

        /** @brief The rule its fills use; non-zero unless set. */
        [[nodiscard]] FillRule GetFillRule() const noexcept
        {
            return m_fillRule;
        }
        /** @brief Sets the rule its fills use. */
        Path& SetFillRule(FillRule rule) noexcept
        {
            m_fillRule = rule;
            return *this;
        }

        /** @brief This path with every point moved by `transform`. */
        [[nodiscard]] Path Transformed(const Transform2D& transform) const;

        /**
         * @brief The outline of this path stroked with `style`, curves and round parts flattened to within `tolerance`
         * UI units: closed polygons, filled non-zero, covering what the stroke covers once. A hairline is stroked one
         * UI unit wide here; the painter strokes it one pixel wide.
         */
        [[nodiscard]] Path StrokeOutline(const StrokeStyle& style, float tolerance = 0.25f) const;

        /** @brief Its steps, in order. */
        [[nodiscard]] std::span<const PathVerb> Verbs() const noexcept
        {
            return m_verbs;
        }
        /** @brief The points its steps take, in order. */
        [[nodiscard]] std::span<const Math::Vec2> Points() const noexcept
        {
            return m_points;
        }
        /** @brief Whether it has no steps. */
        [[nodiscard]] bool IsEmpty() const noexcept
        {
            return m_verbs.empty();
        }

      private:
        friend class Painter;

        /** @brief What a path made by one helper alone is, which the painter strokes analytically. */
        struct Shape
        {
            enum class Kind : std::uint8_t
            {
                Circle,
                RoundedRect,
            };
            Kind kind = Kind::Circle;
            /** @brief The rectangle, or the circle's square. */
            Rect2D rect{};
            /** @brief The rounded rectangle's radii, fitted. */
            CornerRadii radii;
        };

        /** @brief Makes sure a subpath is open, starting one at `point` when there is none. */
        void EnsureStarted(Math::Vec2 point);
        /** @brief Called by every step: the path is no longer one helper's shape alone. */
        void ForgetShape();

        std::vector<PathVerb> m_verbs;
        std::vector<Math::Vec2> m_points;
        FillRule m_fillRule = FillRule::NonZero;
        /** @brief Set when the path is exactly one AddCircle or AddRoundedRect (or AddRect). */
        std::optional<Shape> m_shape;
        /** @brief Whether a subpath is open, and where it started and is now. */
        bool m_open = false;
        Math::Vec2 m_start{0.0f};
        Math::Vec2 m_current{0.0f};
        /** @brief Whether the last step was Close, so the next one starts at `m_start`. */
        bool m_closed = false;
    };
} // namespace Fenrir
