#include "Compass.hpp"

#include <array>
#include <cmath>

#include "FenrirPainter/Painter.hpp"
#include "FenrirPainter/Path.hpp"

namespace UIDemo
{
    namespace
    {
        constexpr float TickDegrees = 15.0f;

        /** @brief `degrees` brought into [-180, 180). */
        float Wrap(float degrees)
        {
            const float wrapped = std::fmod(degrees + 180.0f, 360.0f);
            return (wrapped < 0.0f ? wrapped + 360.0f : wrapped) - 180.0f;
        }
    } // namespace

    void Compass::Paint(Fenrir::Painter& painter, const Fenrir::ElementPaint& paint)
    {
        const Fenrir::Rect2D& box = paint.boxes.content;
        if (box.width <= 0.0f || box.height <= 0.0f)
        {
            return;
        }
        const float centreX = box.x + box.width * 0.5f;
        const float unitsPerDegree = box.width / SpanDegrees;
        const float bottom = box.y + box.height;
        Fenrir::Colour colour = paint.style.color;
        colour.a *= paint.opacity;

        Fenrir::StrokeStyle stroke;
        stroke.cap = Fenrir::LineCap::Round;

        // Every tick within the span, from the first multiple of TickDegrees at or past its left end.
        const float leftHeading = m_heading - SpanDegrees * 0.5f;
        float tick = std::ceil(leftHeading / TickDegrees) * TickDegrees;
        for (; tick <= m_heading + SpanDegrees * 0.5f; tick += TickDegrees)
        {
            const float offset = Wrap(tick - m_heading);
            const float x = centreX + offset * unitsPerDegree;
            const bool cardinal = std::fmod(std::abs(Wrap(tick)), 90.0f) < 0.5f;
            const float length = box.height * (cardinal ? 0.6f : 0.3f);
            stroke.width = cardinal ? 3.0f : 1.5f;
            // Ticks fade towards the strip's ends, so it reads as a band round the heading.
            Fenrir::Colour faded = colour;
            faded.a *= 1.0f - std::abs(offset) / (SpanDegrees * 0.5f);

            Fenrir::Path line;
            line.MoveTo({x, bottom}).LineTo({x, bottom - length});
            painter.StrokePath(line, stroke, faded);

            // North gets a filled triangle over its tick.
            if (std::abs(Wrap(tick)) < 0.5f)
            {
                const float top = bottom - length - 4.0f;
                const std::array<Fenrir::Math::Vec2, 3> marker{
                    Fenrir::Math::Vec2{x - 6.0f, top - 10.0f},
                    Fenrir::Math::Vec2{x + 6.0f, top - 10.0f},
                    Fenrir::Math::Vec2{x, top},
                };
                Fenrir::Path triangle;
                triangle.AddPolygon(marker);
                painter.FillPath(triangle, faded);
            }
        }

        // The baseline, and the needle at the heading.
        stroke.width = 1.0f;
        Fenrir::Colour baseline = colour;
        baseline.a *= 0.5f;
        Fenrir::Path base;
        base.MoveTo({box.x, bottom}).LineTo({box.x + box.width, bottom});
        painter.StrokePath(base, stroke, baseline);

        stroke.width = 2.0f;
        Fenrir::Path needle;
        needle.MoveTo({centreX - 7.0f, box.y}).LineTo({centreX, box.y + 9.0f}).LineTo({centreX + 7.0f, box.y});
        painter.StrokePath(needle, stroke, colour);
    }
} // namespace UIDemo
