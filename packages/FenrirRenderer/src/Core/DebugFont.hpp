#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "FenrirMath/Math.hpp"

namespace Fenrir::Render
{
    /** @brief One glyph of laid-out debug text: where it is drawn and where it sits in the font's atlas. */
    struct GlyphQuad
    {
        /** @brief Its corners in the text's units, from the text's top-left, y down. */
        Math::Vec2 min{0.0f};
        Math::Vec2 max{0.0f};
        /** @brief Its corners in the atlas, from its top-left, in [0, 1]. */
        Math::Vec2 uvMin{0.0f};
        Math::Vec2 uvMax{0.0f};
    };

    /**
     * @brief The renderer's one debug font (ADR-0025): the embedded OFL monospace TrueType font (JetBrains Mono
     * Regular, `Resources/Fonts`), rasterised when made into one single-channel signed-distance-field atlas with
     * stb_truetype. A texel holds 0.5 (128) on a glyph's edge, more inside and less outside, falling by one 255th per
     * 1/32 of the rasterised pixel height. One atlas serves every size. It covers printable ASCII and Latin-1; any
     * other codepoint, or a byte that is not UTF-8, draws `?`.
     */
    class DebugFont
    {
      public:
        /** @brief Rasterises the embedded font into the atlas. */
        DebugFont();

        /** @brief The atlas's size in texels; its rows are top first. */
        [[nodiscard]] std::uint32_t AtlasWidth() const noexcept
        {
            return m_width;
        }
        [[nodiscard]] std::uint32_t AtlasHeight() const noexcept
        {
            return m_height;
        }

        /** @brief The atlas: one byte a texel, rows top first, AtlasWidth texels a row. */
        [[nodiscard]] std::span<const std::byte> AtlasTexels() const noexcept
        {
            return m_texels;
        }

        /**
         * @brief The glyphs of UTF-8 `text` at a line height of `size`, from its top-left, y down: each character one
         * advance on, and a line `size` lower after each `\n`. A blank character (a space) advances without a glyph.
         */
        [[nodiscard]] std::vector<GlyphQuad> Layout(std::string_view text, float size) const;

        /** @brief The width of the longest line of `text` in advances, and its lines' height, at line height `size`. */
        [[nodiscard]] Math::Vec2 Measure(std::string_view text, float size) const;

      private:
        /** @brief One rasterised character: its quad from the pen at the line's top, in raster pixels, and its UVs. */
        struct Glyph
        {
            bool inked = false;
            Math::Vec2 offset{0.0f};
            Math::Vec2 extent{0.0f};
            Math::Vec2 uvMin{0.0f};
            Math::Vec2 uvMax{0.0f};
        };

        /** @brief The glyph a codepoint draws: its own in ASCII and Latin-1, else `?`'s. */
        [[nodiscard]] const Glyph& GlyphOf(char32_t codepoint) const;

        std::uint32_t m_width = 0;
        std::uint32_t m_height = 0;
        std::vector<std::byte> m_texels;
        /** @brief By codepoint, below 256. */
        std::array<Glyph, 256> m_glyphs{};
        /** @brief The advance, in raster pixels. */
        float m_advance = 0.0f;
    };
} // namespace Fenrir::Render
