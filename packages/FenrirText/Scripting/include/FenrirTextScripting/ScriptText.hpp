#pragma once

// What FenrirText's natives take that FenrirText's own headers cannot share as plain data: a paragraph C# made, named
// by a number, and a run's TextStyle without its family, which crosses as text of its own. The natives turn these into
// FenrirText's own. This header is the text binding's own API description (Scripting/api/FenrirTextScripting.api.toml),
// with FenrirText's plain data it names (ParagraphStyle, LineMetrics, Rect2D and the enums), so their C# mirrors and
// mappings are generated from it and checked against C# as scripts load. The doc comments are C#'s.

#include <cstdint>

#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirText/Paragraph.hpp"
#include "FenrirText/TextStyle.hpp"

namespace Fenrir::ScriptText
{
    /**
     * @brief A paragraph of styled text that a script made (Paragraph.Create) and the engine keeps until the script
     * destroys it: plain data, so copying it does not copy the paragraph. Its calls lay it out in the app's FontLibrary
     * and read back its size, lines and hit positions. Ids are never handed out again, so a destroyed paragraph's calls
     * do nothing. The default paragraph names none.
     */
    struct Paragraph
    {
        /** @brief Which paragraph: zero for none. */
        std::uint64_t id = 0;
    };

    /** @brief How one run of a paragraph's text looks, but its family, which a call takes as text. */
    struct TextStyle
    {
        /**
         * @brief The font size in pixels: the em square's height. A paragraph a painter draws is laid out in the
         * target's pixels: the style's size times the root scale times the target's pixels per unit.
         */
        float size = 16.0f;
        /** @brief The weight asked for, 100 to 900; the family's nearest face is used. */
        std::uint16_t weight = 400;
        /** @brief Upright or italic; a family without the style uses its other faces. */
        FontStyle style = FontStyle::Normal;
        /** @brief The text's colour, straight alpha. */
        Colour colour;
        /** @brief Pixels added after each glyph that advances. Not zero turns ligatures off. */
        float letterSpacing = 0.0f;
        /** @brief The line drawn with the run. */
        TextDecoration decoration = TextDecoration::None;
    };
} // namespace Fenrir::ScriptText
