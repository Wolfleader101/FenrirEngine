#pragma once

#include <string>
#include <string_view>

#include "FenrirMath/Math.hpp"
#include "FenrirText/Paragraph.hpp"
#include "FenrirText/TextStyle.hpp"
#include "FenrirUI/Element.hpp"
#include "FenrirUI/Style.hpp"

namespace Fenrir
{
    class FontLibrary;
}

namespace Fenrir::Detail
{
    /**
     * @brief An element's text: its FenrirText paragraph, laid out in the target's pixels and measured
     * back in UI units. The paragraph's sizes are the style's times the target's pixels per UI unit (the root's scale
     * times the target's content scale, `size * scale * pixelsPerUnit`), the exact pixel size its bitmaps are
     * rasterised at, so the painter's DrawText puts whole-pixel glyphs on whole pixels.
     */
    class ElementText
    {
      public:
        /** @brief Makes `text` the whole text, in the style last given. */
        void SetText(std::string_view text);

        /** @brief The text, as set. */
        [[nodiscard]] const std::string& Text() const noexcept
        {
            return m_text;
        }

        /**
         * @brief Keeps it on one line whatever its style says, as a text entry's: every space kept, no wrapping, no
         * ellipsis, aligned to the start. Taken at the next SetStyle.
         */
        void SetSingleLine(bool singleLine) noexcept
        {
            m_singleLine = singleLine;
        }

        /**
         * @brief Takes the text properties of `style` at `pixelsPerUnit` target pixels per UI unit. True when what
         * changed can change the paragraph's size, so layout must measure it again.
         */
        bool SetStyle(const ResolvedStyle& style, float pixelsPerUnit);

        /**
         * @brief The content box's size in UI units for `constraints`, the paragraph laid out with `fonts` to their
         * width. Nothing is laid out without fonts.
         */
        [[nodiscard]] Math::Vec2 Measure(FontLibrary* fonts, const MeasureConstraints& constraints);

        /** @brief Sets the width, in UI units, the paragraph is drawn at: its content box's, as laid out. */
        void SetContentWidth(float width) noexcept
        {
            m_contentWidth = width;
        }

        /**
         * @brief Lays the paragraph out at its content width for drawing, unless nothing changed since. True when it
         * changed size with no measure since (the font library's families changed), so layout must measure it again.
         */
        bool LayOutForDrawing(FontLibrary& fonts);

        /** @brief The paragraph, laid out in target pixels. */
        [[nodiscard]] Paragraph& GetParagraph() noexcept
        {
            return m_paragraph;
        }

        /** @copydoc GetParagraph */
        [[nodiscard]] const Paragraph& GetParagraph() const noexcept
        {
            return m_paragraph;
        }

      private:
        std::string m_text;
        Paragraph m_paragraph;
        TextStyle m_style;
        ParagraphStyle m_paragraphStyle;
        /** @brief Target pixels per UI unit. */
        float m_pixelsPerUnit = 1.0f;
        /** @brief The content box's unsnapped width in UI units; infinite before the first layout. */
        float m_contentWidth = UnlimitedWidth;
        /** @brief The width, in target pixels, it was last drawn at, and its size there; negative after a measure. */
        float m_drawnWidth = -1.0f;
        Math::Vec2 m_drawnSize{0.0f};
        bool m_singleLine = false;
    };
} // namespace Fenrir::Detail
