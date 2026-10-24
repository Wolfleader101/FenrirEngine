#include "Core/DebugFont.hpp"

#include <algorithm>
#include <cstring>
#include <span>

#include <stb_truetype.h>

#include "FenrirResources/DebugFont.hpp"

namespace Fenrir::Render
{
    namespace
    {
        /** @brief The pixel height glyphs are rasterised at: ascent to descent, a line's height. */
        constexpr float RasterHeight = 32.0f;
        /** @brief Texels of distance field round each glyph, so its edge fades out inside its own box. */
        constexpr int Padding = 4;
        /** @brief The texel value on a glyph's edge, and how much it changes per raster pixel. */
        constexpr unsigned char OnEdge = 128;
        constexpr float DistanceScale = static_cast<float>(OnEdge) / static_cast<float>(Padding);
        /** @brief The atlas's width; it is as tall as its rows of glyphs need. */
        constexpr std::uint32_t AtlasTexelsWide = 512;

        /** @brief Whether the font covers `codepoint`: printable ASCII and Latin-1 (ADR-0025). */
        constexpr bool Covered(char32_t codepoint)
        {
            return (codepoint >= 0x20 && codepoint <= 0x7E) || (codepoint >= 0xA0 && codepoint <= 0xFF);
        }

        /** @brief A distance field stb_truetype made, `width` x `height` bytes; the one place its pointer is trusted.
         */
        std::span<const unsigned char> FieldOf(const unsigned char* field, int width, int height)
        {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
            return {field, static_cast<std::size_t>(width) * static_cast<std::size_t>(height)};
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
        }

        /** @brief Calls `visit(codepoint)` for each character of UTF-8 `text`; a byte that is not UTF-8 gives U+FFFD.
         */
        template <typename Visit>
        void ForEachCodepoint(std::string_view text, const Visit& visit)
        {
            std::size_t i = 0;
            while (i < text.size())
            {
                const auto lead = static_cast<std::uint8_t>(text[i]);
                std::size_t length = 1;
                char32_t codepoint = lead;
                if (lead >= 0xC0 && lead < 0xE0)
                {
                    length = 2;
                    codepoint = lead & 0x1Fu;
                }
                else if (lead >= 0xE0 && lead < 0xF0)
                {
                    length = 3;
                    codepoint = lead & 0x0Fu;
                }
                else if (lead >= 0xF0 && lead < 0xF8)
                {
                    length = 4;
                    codepoint = lead & 0x07u;
                }
                else if (lead >= 0x80)
                {
                    length = 0;
                }
                for (std::size_t k = 1; length != 0 && k < length; ++k)
                {
                    const auto next = i + k < text.size() ? static_cast<std::uint8_t>(text[i + k]) : std::uint8_t{0};
                    if ((next & 0xC0u) != 0x80u)
                    {
                        length = 0;
                    }
                    else
                    {
                        codepoint = (codepoint << 6u) | (next & 0x3Fu);
                    }
                }
                if (length == 0)
                {
                    visit(U'�');
                    ++i;
                    continue;
                }
                visit(codepoint);
                i += length;
            }
        }
    } // namespace

    DebugFont::DebugFont()
    {
        stbtt_fontinfo info{};
        if (stbtt_InitFont(&info, static_cast<const unsigned char*>(Resources::DebugFont), 0) == 0)
        {
            return;
        }
        const float scale = stbtt_ScaleForPixelHeight(&info, RasterHeight);
        int ascent = 0;
        int descent = 0;
        int lineGap = 0;
        stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
        const float baseline = static_cast<float>(ascent) * scale;
        int advance = 0;
        int bearing = 0;
        stbtt_GetCodepointHMetrics(&info, '?', &advance, &bearing);
        m_advance = static_cast<float>(advance) * scale;

        // Each covered glyph's distance field, packed in rows (shelves) across the atlas.
        struct Raster
        {
            char32_t codepoint = 0;
            unsigned char* field = nullptr;
            int width = 0;
            int height = 0;
            std::uint32_t x = 0;
            std::uint32_t y = 0;
        };
        std::vector<Raster> rasters;
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        std::uint32_t rowHeight = 0;
        for (char32_t codepoint = 0; codepoint < m_glyphs.size(); ++codepoint)
        {
            if (!Covered(codepoint))
            {
                continue;
            }
            Raster raster;
            raster.codepoint = codepoint;
            int xoff = 0;
            int yoff = 0;
            raster.field = stbtt_GetCodepointSDF(&info, scale, static_cast<int>(codepoint), Padding, OnEdge,
                                                 DistanceScale, &raster.width, &raster.height, &xoff, &yoff);
            if (raster.field == nullptr)
            {
                continue;
            }
            const auto width = static_cast<std::uint32_t>(raster.width);
            const auto height = static_cast<std::uint32_t>(raster.height);
            if (x + width > AtlasTexelsWide)
            {
                x = 0;
                y += rowHeight;
                rowHeight = 0;
            }
            raster.x = x;
            raster.y = y;
            x += width;
            rowHeight = std::max(rowHeight, height);
            Glyph& glyph = m_glyphs.at(codepoint);
            glyph.inked = true;
            glyph.offset = {static_cast<float>(xoff), baseline + static_cast<float>(yoff)};
            glyph.extent = {static_cast<float>(raster.width), static_cast<float>(raster.height)};
            rasters.push_back(raster);
        }

        m_width = AtlasTexelsWide;
        m_height = y + rowHeight;
        m_texels.assign(std::size_t{m_width} * m_height, std::byte{0});
        for (const Raster& raster : rasters)
        {
            const std::span<const unsigned char> field = FieldOf(raster.field, raster.width, raster.height);
            const auto width = static_cast<std::size_t>(raster.width);
            for (std::size_t row = 0; row < static_cast<std::size_t>(raster.height); ++row)
            {
                std::memcpy(&m_texels[(std::size_t{raster.y} + row) * m_width + raster.x],
                            field.subspan(row * width, width).data(), width);
            }
            Glyph& glyph = m_glyphs.at(raster.codepoint);
            glyph.uvMin = {static_cast<float>(raster.x) / static_cast<float>(m_width),
                           static_cast<float>(raster.y) / static_cast<float>(m_height)};
            glyph.uvMax = {static_cast<float>(raster.x + static_cast<std::uint32_t>(raster.width)) /
                               static_cast<float>(m_width),
                           static_cast<float>(raster.y + static_cast<std::uint32_t>(raster.height)) /
                               static_cast<float>(m_height)};
            stbtt_FreeSDF(raster.field, nullptr);
        }
    }

    const DebugFont::Glyph& DebugFont::GlyphOf(char32_t codepoint) const
    {
        return m_glyphs.at(Covered(codepoint) ? codepoint : U'?');
    }

    std::vector<GlyphQuad> DebugFont::Layout(std::string_view text, float size) const
    {
        std::vector<GlyphQuad> quads;
        const float scale = size / RasterHeight;
        Math::Vec2 pen(0.0f);
        ForEachCodepoint(text, [&](char32_t codepoint) {
            if (codepoint == U'\n')
            {
                pen = {0.0f, pen.y + size};
                return;
            }
            const Glyph& glyph = GlyphOf(codepoint);
            if (glyph.inked)
            {
                const Math::Vec2 min = pen + glyph.offset * scale;
                quads.push_back({min, min + glyph.extent * scale, glyph.uvMin, glyph.uvMax});
            }
            pen.x += m_advance * scale;
        });
        return quads;
    }

    Math::Vec2 DebugFont::Measure(std::string_view text, float size) const
    {
        std::size_t columns = 0;
        std::size_t widest = 0;
        std::size_t lines = text.empty() ? 0 : 1;
        ForEachCodepoint(text, [&](char32_t codepoint) {
            if (codepoint == U'\n')
            {
                ++lines;
                columns = 0;
                return;
            }
            widest = std::max(widest, ++columns);
        });
        return {static_cast<float>(widest) * m_advance * size / RasterHeight, static_cast<float>(lines) * size};
    }
} // namespace Fenrir::Render
