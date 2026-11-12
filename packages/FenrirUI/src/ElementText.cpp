#include "ElementText.hpp"

#include <algorithm>
#include <cmath>

#include "FenrirText/FontLibrary.hpp"

namespace Fenrir::Detail
{
    namespace
    {
        /**
         * @brief Pixels a paragraph is laid out wider than its box: the box's width came back from the paragraph's
         * through Yoga's float arithmetic, which may take a hair off it, and the text that fitted must still fit.
         */
        constexpr float WidthSlack = 1.0f / 64.0f;

        /** @brief The width in target pixels a paragraph is laid out to for `units` UI units of box. */
        float LayoutWidth(float units, float pixelsPerUnit)
        {
            return std::max(units, 0.0f) * pixelsPerUnit + WidthSlack;
        }

        /** @brief Whether two styles lay text out to the same size: all but the colour. */
        bool SameShape(const TextStyle& a, const TextStyle& b)
        {
            return a.family == b.family && a.size == b.size && a.weight == b.weight && a.style == b.style &&
                   a.letterSpacing == b.letterSpacing && a.decoration == b.decoration;
        }

        bool SameColour(const Colour& a, const Colour& b)
        {
            return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
        }

        /** @brief Whether two paragraph styles lay text out to the same size: all but the alignment. */
        bool SameShape(const ParagraphStyle& a, const ParagraphStyle& b)
        {
            ParagraphStyle aligned = a;
            aligned.align = b.align;
            return aligned == b;
        }

        /** @brief `line-height` as FenrirText's multiple of the font size; zero for `normal`. */
        float LineHeightOf(const ResolvedStyle& style)
        {
            switch (style.lineHeight.unit)
            {
            case LengthUnit::Auto:
                return 0.0f;
            case LengthUnit::Percent:
                return std::max(style.lineHeight.value, 0.0f) / 100.0f;
            case LengthUnit::Px:
                return style.fontSize > 0.0f ? std::max(style.lineHeight.value, 0.0f) / style.fontSize : 0.0f;
            }
            return 0.0f;
        }

        float Constrained(float size, float limit, MeasureMode mode)
        {
            switch (mode)
            {
            case MeasureMode::Exactly:
                return limit;
            case MeasureMode::AtMost:
                return std::min(size, limit);
            case MeasureMode::Undefined:
                break;
            }
            return size;
        }
    } // namespace

    void ElementText::SetText(std::string_view text)
    {
        m_text = std::string(text);
        m_paragraph.SetText(m_text, m_style);
    }

    bool ElementText::SetStyle(const ResolvedStyle& style, float pixelsPerUnit)
    {
        pixelsPerUnit = pixelsPerUnit > 0.0f ? pixelsPerUnit : 1.0f;
        TextStyle text;
        text.family = style.fontFamily;
        text.size = std::max(style.fontSize, 0.0f) * pixelsPerUnit;
        text.weight = style.fontWeight;
        text.style = style.fontStyle;
        text.colour = style.color;
        text.letterSpacing = style.letterSpacing * pixelsPerUnit;

        ParagraphStyle paragraph;
        paragraph.align = style.textAlign;
        paragraph.lineHeight = LineHeightOf(style);
        paragraph.ellipsis = style.textOverflow == TextOverflow::Ellipsis;
        paragraph.whiteSpace = style.whiteSpace;

        const bool resized =
            !SameShape(text, m_style) || !SameShape(paragraph, m_paragraphStyle) || pixelsPerUnit != m_pixelsPerUnit;
        if (resized || !SameColour(text.colour, m_style.colour))
        {
            m_style = std::move(text);
            m_paragraph.SetText(m_text, m_style);
        }
        m_paragraphStyle = paragraph;
        m_paragraph.SetStyle(paragraph);
        m_pixelsPerUnit = pixelsPerUnit;
        return resized;
    }

    Math::Vec2 ElementText::Measure(FontLibrary* fonts, const MeasureConstraints& constraints)
    {
        if (fonts == nullptr)
        {
            return Math::Vec2(0.0f);
        }
        const float width = constraints.widthMode == MeasureMode::Undefined
                                ? UnlimitedWidth
                                : LayoutWidth(constraints.width, m_pixelsPerUnit);
        m_paragraph.Layout(*fonts, width);
        // Layout has seen the paragraph's size: drawing it at another width next is no news.
        m_drawnWidth = -1.0f;
        const Math::Vec2 size = m_paragraph.Size() / m_pixelsPerUnit;
        return {Constrained(size.x, constraints.width, constraints.widthMode),
                Constrained(size.y, constraints.height, constraints.heightMode)};
    }

    bool ElementText::LayOutForDrawing(FontLibrary& fonts)
    {
        const float width =
            std::isfinite(m_contentWidth) ? LayoutWidth(m_contentWidth, m_pixelsPerUnit) : UnlimitedWidth;
        const bool relaid = m_paragraph.Layout(fonts, width);
        // Laid out again at the width it was drawn at, with no measure since: the fonts changed under it, and layout
        // has not seen its new size.
        const bool resized = relaid && width == m_drawnWidth && m_paragraph.Size() != m_drawnSize;
        m_drawnWidth = width;
        m_drawnSize = m_paragraph.Size();
        return resized;
    }
} // namespace Fenrir::Detail
