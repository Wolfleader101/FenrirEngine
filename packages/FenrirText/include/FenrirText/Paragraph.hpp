#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirText/GlyphAtlas.hpp"
#include "FenrirText/TextStyle.hpp"

namespace Fenrir
{
    class FontLibrary;

    /** @brief One glyph of a laid-out paragraph, with its place on screen and in its run's atlas page. */
    struct PositionedGlyph
    {
        /** @brief The glyph's index in its face. */
        std::uint32_t glyph = 0;
        /** @brief The byte offset, in the paragraph's text, of the first character the glyph draws. */
        std::uint32_t cluster = 0;
        /**
         * @brief The pen position the glyph is drawn from, on its line's baseline, in pixels from the paragraph's
         * top-left; x is a multiple of a quarter pixel.
         */
        Math::Vec2 origin{0.0f};
        /**
         * @brief Where the glyph's atlas rectangle is drawn, from the paragraph's top-left: whole pixels in bitmap
         * mode; in SDF mode the rectangle scaled from the SDF size, its spread included, in fractional pixels.
         */
        Rect2D quad;
        /** @brief The glyph's rectangle in its run's atlas page. */
        AtlasRegion atlas;
    };

    /**
     * @brief Glyphs a paragraph draws in one face, size, style and atlas page, on one line, left to right. Glyphs with
     * nothing to draw (spaces) are left out; the run's extent still covers them, for decorations.
     */
    struct GlyphRun
    {
        /** @brief The font the glyphs are from. */
        AssetHandle<Font> font;
        /** @brief The font size in pixels. */
        float size = 0.0f;
        /** @brief The atlas page the glyphs' rectangles are on. */
        AssetHandle<Image> page;
        /** @brief The face's mode: the page holds coverage (Bitmap) or a distance field (Sdf). */
        FontRendering rendering = FontRendering::Bitmap;
        /**
         * @brief In SDF mode, the distance field's spread in the page's texels: a texel of byte value `v` lies
         * `(v - 128) / 128` spreads inside the outline (negative outside), as FreeType's SDF mode stores it. Zero in
         * bitmap mode.
         */
        float sdfSpread = 0.0f;
        /** @brief The run's text style's colour. */
        Colour colour;
        /** @brief The run's text style's decoration, drawn from the metrics below. */
        TextDecoration decoration = TextDecoration::None;
        /** @brief The line, an index into Paragraph::Lines. */
        std::uint32_t line = 0;
        /** @brief Whether the run's text reads right to left. */
        bool rightToLeft = false;
        /** @brief The left and width of the run's advances, in pixels from the paragraph's left. */
        float left = 0.0f;
        float width = 0.0f;
        /** @brief The line's baseline, in pixels from the paragraph's top. */
        float baseline = 0.0f;
        /** @brief The face's underline: its centre below the baseline, and its thickness, in pixels. */
        float underlineOffset = 0.0f;
        float underlineThickness = 0.0f;
        /** @brief The face's ascent above the baseline, in pixels, where a line-through or overline is drawn from. */
        float ascent = 0.0f;
        std::vector<PositionedGlyph> glyphs;
    };

    /** @brief One laid-out line of a paragraph. */
    struct LineMetrics
    {
        /** @brief The byte range of the paragraph's text on the line, its trailing spaces and newline included. */
        std::uint32_t begin = 0;
        std::uint32_t end = 0;
        /** @brief The line box's top and height, in pixels from the paragraph's top. */
        float top = 0.0f;
        float height = 0.0f;
        /** @brief The baseline, in pixels from the paragraph's top. */
        float baseline = 0.0f;
        /** @brief The largest ascent above the baseline and descent below it of the line's faces, in pixels. */
        float ascent = 0.0f;
        float descent = 0.0f;
        /** @brief Where the line's text starts after alignment, and its width without trailing spaces, in pixels. */
        float left = 0.0f;
        float width = 0.0f;
    };

    namespace Detail
    {
        struct ParagraphLayout;
    }

    /**
     * @brief A paragraph of styled UTF-8 text laid out to a width (ADR-0062): its runs split by style, script, bidi
     * level (SheenBidi) and face, shaped by HarfBuzz, broken into lines at libunibreak's UAX#14 opportunities, ordered
     * per line by bidi level, and aligned. It answers its size, lines, glyph runs (rasterised into the font library's
     * atlases as it lays out), the rectangles of a character range, the character at a point, and its UAX#29 grapheme
     * boundaries. Character positions are byte offsets into Text().
     *
     * A laid-out paragraph is kept across frames: Layout lays out again only when the text, a style, the width or the
     * library's families changed since it last did.
     */
    class Paragraph
    {
      public:
        /** @brief A paragraph with no text, in the default paragraph style. */
        Paragraph();
        ~Paragraph();
        Paragraph(const Paragraph&) = delete;
        Paragraph& operator=(const Paragraph&) = delete;
        Paragraph(Paragraph&&) noexcept;
        Paragraph& operator=(Paragraph&&) noexcept;

        /** @brief Makes `utf8` in `style` the whole text. */
        void SetText(std::string_view utf8, const TextStyle& style);

        /** @brief Appends `utf8` in `style` to the text. */
        void AddRun(std::string_view utf8, const TextStyle& style);

        /** @brief Removes all text. */
        void Clear();

        /** @brief Sets how the lines are laid out; the next Layout lays out again if it differs. */
        void SetStyle(const ParagraphStyle& style);

        /** @brief How the lines are laid out. */
        [[nodiscard]] const ParagraphStyle& Style() const noexcept;

        /** @brief The text, every run's in order. */
        [[nodiscard]] const std::string& Text() const noexcept;

        /**
         * @brief Lays the paragraph out to `width` pixels (UnlimitedWidth for no limit) with `fonts`' faces, unless
         * nothing changed since it last did. True when it laid out. Glyphs not yet in an atlas are rasterised into it.
         */
        bool Layout(FontLibrary& fonts, float width);

        /** @brief The laid-out text's extent: its widest line's width and its lines' height, in pixels. */
        [[nodiscard]] Math::Vec2 Size() const noexcept;

        /** @brief The laid-out lines, top to bottom; empty before Layout and for no text. */
        [[nodiscard]] std::span<const LineMetrics> Lines() const noexcept;

        /** @brief The glyph runs, line by line and left to right within a line; empty before Layout. */
        [[nodiscard]] std::span<const GlyphRun> Runs() const noexcept;

        /**
         * @brief The rectangles covering the characters in the byte range [`begin`, `end`): one per line and run of
         * the range that is contiguous on screen, left to right within a line, each a line box tall. Empty for an empty
         * range or before Layout.
         */
        [[nodiscard]] std::vector<Rect2D> RangeRects(std::uint32_t begin, std::uint32_t end) const;

        /**
         * @brief The caret position nearest `point` (pixels from the paragraph's top-left): the grapheme boundary at
         * the leading edge of the character under the point, or at its trailing edge past its middle. A point above or
         * below the text takes the first or last line; left or right of a line, its visual start or end.
         */
        [[nodiscard]] std::uint32_t CharacterAt(Math::Vec2 point) const;

        /** @brief The text's grapheme boundaries (UAX#29), ascending, from 0 to the text's size. */
        [[nodiscard]] std::span<const std::uint32_t> GraphemeBoundaries() const noexcept;

        /** @brief The first grapheme boundary after `index`, or the text's size. */
        [[nodiscard]] std::uint32_t NextGrapheme(std::uint32_t index) const;

        /** @brief The last grapheme boundary before `index`, or zero. */
        [[nodiscard]] std::uint32_t PreviousGrapheme(std::uint32_t index) const;

      private:
        void TextChanged();

        struct Run
        {
            std::uint32_t begin = 0;
            std::uint32_t end = 0;
            TextStyle style;
        };

        std::string m_text;
        std::vector<Run> m_runs;
        ParagraphStyle m_style;
        std::vector<std::uint32_t> m_graphemes;
        std::unique_ptr<Detail::ParagraphLayout> m_layout;
    };
} // namespace Fenrir
