#include "Compass.hpp"

#include <array>
#include <cmath>

#include "FenrirApp/App.hpp"
#include "FenrirPainter/Painter.hpp"
#include "FenrirPainter/Path.hpp"
#include "FenrirText/FontLibrary.hpp"
#include "FenrirText/TextStyle.hpp"
#include "FenrirUI/UI.hpp"
#include "FenrirUI/UIRoot.hpp"
#include "UIDemo.hpp"

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
        // The letters lay out in the target's pixels (Painter's text), which the painter divides back to UI units.
        const Fenrir::UIRoot& root = GetRoot();
        const auto* roots = root.GetApp().TryGetResource<Fenrir::UIRoots>();
        Fenrir::FontLibrary* fonts = roots != nullptr ? roots->GetFonts() : nullptr;
        const float pixels = root.GetScale() * root.GetTargetSize().contentScale;
        constexpr std::array<const char*, 4> letters{"N", "E", "S", "W"};

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

            // A cardinal point's letter over its tick, in the SDF family.
            if (cardinal && fonts != nullptr && pixels > 0.0f)
            {
                const auto quarter = static_cast<std::size_t>(std::lround((Wrap(tick) + 360.0f) / 90.0f)) % 4;
                Fenrir::Paragraph& label = m_labels.at(quarter);
                Fenrir::TextStyle text;
                text.family = SdfFamily;
                text.size = LabelSize * pixels;
                text.weight = Fenrir::FontWeightBold;
                text.colour = faded;
                label.SetText(letters.at(quarter), text);
                label.Layout(*fonts, Fenrir::UnlimitedWidth);
                const Fenrir::Math::Vec2 size = label.Size() / pixels;
                painter.DrawText(label, {x - size.x * 0.5f, bottom - length - size.y});
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
