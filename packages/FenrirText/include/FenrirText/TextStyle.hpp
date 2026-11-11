#pragma once

#include <cstdint>
#include <limits>
#include <string>

#include "FenrirMath/Colour.hpp"

namespace Fenrir
{
    /** @name Font weights
     * The CSS weights a family's faces are registered and asked for by, from 100 (thin) to 900 (black).
     * @{
     */
    inline constexpr std::uint16_t FontWeightNormal = 400;
    inline constexpr std::uint16_t FontWeightBold = 700;
    /** @} */

    /** @brief Whether a face is upright or italic. */
    enum class FontStyle : std::uint8_t
    {
        Normal,
        Italic,
    };

    /** @brief A line drawn with a run of text, as CSS's `text-decoration-line`. FenrirText only reports it. */
    enum class TextDecoration : std::uint8_t
    {
        None,
        Underline,
        LineThrough,
        Overline,
    };

    /** @brief How one run of a paragraph's text looks. */
    struct TextStyle
    {
        /**
         * @brief The font family's name, as registered with FontLibrary::AddFace. A name with no faces resolves to the
         * embedded default face.
         */
        std::string family = "default";
        /** @brief The font size in pixels: the em square's height. */
        float size = 16.0f;
        /** @brief The weight asked for, 100 to 900; the family's nearest face is used. */
        std::uint16_t weight = FontWeightNormal;
        /** @brief Upright or italic; a family without the style uses its other faces. */
        FontStyle style = FontStyle::Normal;
        /** @brief The text's colour, reported on its glyph runs. */
        Colour colour;
        /**
         * @brief Pixels added after each glyph that advances. Not zero turns ligatures off, as CSS does, so that the
         * letters it spaces stay letters.
         */
        float letterSpacing = 0.0f;
        /** @brief The line drawn with the run, reported on its glyph runs. */
        TextDecoration decoration = TextDecoration::None;
    };

    /** @brief Where a paragraph's lines sit across its width. */
    enum class TextAlign : std::uint8_t
    {
        /** @brief Left in a left-to-right paragraph, right in a right-to-left one. */
        Start,
        /** @brief Right in a left-to-right paragraph, left in a right-to-left one. */
        End,
        Left,
        Right,
        Center,
    };

    /** @brief How a paragraph treats spaces, tabs and newlines, as CSS's `white-space`. */
    enum class WhiteSpace : std::uint8_t
    {
        /** @brief Spaces, tabs and newlines collapse to one space, and lines wrap at the width. */
        Normal,
        /** @brief Collapsed as Normal, on one line. */
        NoWrap,
        /** @brief Every space is kept, newlines break lines, and lines never wrap. */
        Pre,
        /** @brief Kept as Pre, and lines wrap at the width. */
        PreWrap,
    };

    /** @brief A paragraph's base direction. */
    enum class TextDirection : std::uint8_t
    {
        /** @brief From its first strong character (Unicode's rule P2), left to right if it has none. */
        Auto,
        LeftToRight,
        RightToLeft,
    };

    /** @brief How a paragraph's lines are laid out. */
    struct ParagraphStyle
    {
        TextAlign align = TextAlign::Start;
        /**
         * @brief A line's height as a multiple of its largest font size; zero means its fonts' own height (ascent,
         * descent and line gap). The space above the tallest glyphs and below is split evenly, as CSS's half-leading.
         */
        float lineHeight = 0.0f;
        /** @brief The most lines laid out; zero means no limit. */
        std::uint32_t maxLines = 0;
        /**
         * @brief Whether text cut off (by maxLines, or past the width on a line that does not wrap) ends its last line
         * with an ellipsis, `…`, in the style there.
         */
        bool ellipsis = false;
        WhiteSpace whiteSpace = WhiteSpace::Normal;
        TextDirection direction = TextDirection::Auto;

        friend bool operator==(const ParagraphStyle&, const ParagraphStyle&) = default;
    };

    /** @brief The width a paragraph lays out to when nothing limits it: lines break only where they must. */
    inline constexpr float UnlimitedWidth = std::numeric_limits<float>::infinity();
} // namespace Fenrir
