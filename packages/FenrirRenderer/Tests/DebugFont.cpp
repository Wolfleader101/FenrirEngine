// The renderer core's debug font (ADR-0025): the embedded OFL monospace TrueType font rasterised at start-up into one
// signed-distance-field atlas, and the layout of debug text in it. No device or App is needed.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/DebugFont.hpp"

using Fenrir::Render::DebugFont;
using Fenrir::Render::GlyphQuad;

namespace
{
    /** @brief `codepoint` (below U+10000) as UTF-8. */
    std::string Utf8(char32_t codepoint)
    {
        std::string text;
        if (codepoint < 0x80)
        {
            text += static_cast<char>(codepoint);
        }
        else if (codepoint < 0x800)
        {
            text += static_cast<char>(0xC0 | (codepoint >> 6));
            text += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        else
        {
            text += static_cast<char>(0xE0 | (codepoint >> 12));
            text += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            text += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        return text;
    }

    bool SameGlyph(const GlyphQuad& a, const GlyphQuad& b)
    {
        return a.uvMin == b.uvMin && a.uvMax == b.uvMax;
    }

    /** @brief Whether the atlas texels under a quad hold both a glyph's inside and its outside. */
    bool Inked(const DebugFont& font, const GlyphQuad& quad)
    {
        const auto x0 = static_cast<std::uint32_t>(quad.uvMin.x * static_cast<float>(font.AtlasWidth()));
        const auto x1 = static_cast<std::uint32_t>(quad.uvMax.x * static_cast<float>(font.AtlasWidth()));
        const auto y0 = static_cast<std::uint32_t>(quad.uvMin.y * static_cast<float>(font.AtlasHeight()));
        const auto y1 = static_cast<std::uint32_t>(quad.uvMax.y * static_cast<float>(font.AtlasHeight()));
        bool inside = false;
        bool outside = false;
        for (std::uint32_t y = y0; y < y1; ++y)
        {
            for (std::uint32_t x = x0; x < x1; ++x)
            {
                const auto texel =
                    static_cast<std::uint8_t>(font.AtlasTexels()[std::size_t{y} * font.AtlasWidth() + x]);
                inside = inside || texel > 140;
                outside = outside || texel < 96;
            }
        }
        return inside && outside;
    }
} // namespace

TEST_CASE("The debug font's atlas draws every printable ASCII and Latin-1 character as its own glyph",
          "[renderer][debug][font]")
{
    const DebugFont font;
    INFO("the embedded font rasterises into an atlas");
    REQUIRE(font.AtlasWidth() > 0);
    REQUIRE(font.AtlasTexels().size() == std::size_t{font.AtlasWidth()} * font.AtlasHeight());
    const std::vector<GlyphQuad> question = font.Layout("?", 16.0f);
    REQUIRE(question.size() == 1);

    std::vector<char32_t> codepoints;
    for (char32_t c = 0x21; c <= 0x7E; ++c)
    {
        codepoints.push_back(c);
    }
    for (char32_t c = 0xA1; c <= 0xFF; ++c)
    {
        // The soft hyphen shows only where a line breaks, so it has no ink of its own.
        if (c != 0xAD)
        {
            codepoints.push_back(c);
        }
    }
    for (const char32_t c : codepoints)
    {
        const std::vector<GlyphQuad> quads = font.Layout(Utf8(c), 16.0f);
        INFO("U+" << std::hex << static_cast<std::uint32_t>(c) << " draws one inked glyph of its own, not '?'");
        REQUIRE(quads.size() == 1);
        REQUIRE((c == U'?' || !SameGlyph(quads[0], question[0])));
        REQUIRE(Inked(font, quads[0]));
    }
}

TEST_CASE("A codepoint outside ASCII and Latin-1, or a byte that is not UTF-8, draws '?'", "[renderer][debug][font]")
{
    const DebugFont font;
    const std::vector<GlyphQuad> question = font.Layout("?", 16.0f);
    REQUIRE(question.size() == 1);
    for (const std::string& text : {Utf8(0x20AC), Utf8(0x0416), std::string("\xFF")})
    {
        const std::vector<GlyphQuad> quads = font.Layout(text, 16.0f);
        INFO("'" << text << "' draws one glyph, the question mark's");
        REQUIRE(quads.size() == 1);
        REQUIRE(SameGlyph(quads[0], question[0]));
    }
}

TEST_CASE("Debug text lays out one advance a character and a line a size", "[renderer][debug][font]")
{
    const DebugFont font;
    const std::vector<GlyphQuad> quads = font.Layout("H H\nH", 20.0f);
    INFO("the space draws nothing, so three glyphs");
    REQUIRE(quads.size() == 3);
    const float advance = (quads[1].min.x - quads[0].min.x) / 2.0f;
    INFO("a monospace advance is a little over half the size");
    REQUIRE(advance > 0.4f * 20.0f);
    REQUIRE(advance < 0.8f * 20.0f);
    INFO("the line after the newline starts back at the left, 20 pixels lower");
    REQUIRE(std::abs(quads[2].min.x - quads[0].min.x) < 1e-4f);
    REQUIRE(std::abs(quads[2].min.y - quads[0].min.y - 20.0f) < 1e-4f);
    INFO("the first line's glyphs sit inside the first line, from y 0 to the size");
    REQUIRE(quads[0].min.y >= 0.0f);
    REQUIRE(quads[0].max.y <= 20.0f);
    INFO("the text measures three advances wide and two lines high");
    REQUIRE(std::abs(font.Measure("H H\nH", 20.0f).x - 3.0f * advance) < 1e-4f);
    REQUIRE(std::abs(font.Measure("H H\nH", 20.0f).y - 40.0f) < 1e-4f);
}
