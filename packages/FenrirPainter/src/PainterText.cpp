// The painter's text (ADR-0060, ADR-0062): FenrirText's glyph runs as quads of shape kind Glyph (bitmap mode) or
// SdfGlyph (SDF mode) over their atlas rectangles, and their decorations as rectangles.

#include <algorithm>
#include <cmath>

#include "Colours.hpp"
#include "FenrirPainter/Painter.hpp"
#include "FenrirText/FontLibrary.hpp"
#include "FenrirText/Paragraph.hpp"

namespace Fenrir
{
    namespace
    {
        /**
         * @brief How far above the baseline a line-through's centre is, as a share of the face's ascent: about half
         * the x-height of a Latin face, where browsers draw it from the face's strikeout metrics.
         */
        constexpr float LineThroughShareOfAscent = 0.3f;
    } // namespace

    void Painter::LayOutIfNever(Paragraph& paragraph) const
    {
        if (!paragraph.Text().empty() && paragraph.Lines().empty() && m_config.fonts != nullptr)
        {
            paragraph.Layout(*m_config.fonts, UnlimitedWidth);
        }
    }

    void Painter::DrawText(Paragraph& paragraph, Math::Vec2 point)
    {
        LayOutIfNever(paragraph);
        const Math::Vec2 origin = SnapPoint(point);
        for (const GlyphRun& run : paragraph.Runs())
        {
            DrawRun(run, origin, run.colour);
        }
    }

    void Painter::DrawText(Paragraph& paragraph, Math::Vec2 point, const TextShadow& shadow)
    {
        LayOutIfNever(paragraph);
        const Math::Vec2 shadowOrigin = SnapPoint(point + shadow.offset);
        for (const GlyphRun& run : paragraph.Runs())
        {
            DrawRun(run, shadowOrigin, shadow.colour);
        }
        DrawText(paragraph, point);
    }

    void Painter::DrawGlyphs(const GlyphRun& run, Math::Vec2 point)
    {
        DrawRun(run, SnapPoint(point), run.colour);
    }

    void Painter::DrawGlyphs(const GlyphRun& run, Math::Vec2 point, const TextShadow& shadow)
    {
        DrawRun(run, SnapPoint(point + shadow.offset), shadow.colour);
        DrawGlyphs(run, point);
    }

    void Painter::DrawRun(const GlyphRun& run, Math::Vec2 point, const Colour& colour)
    {
        const float pixelsPerUiUnit = m_config.scale * PixelsPerUnit();
        if (!(pixelsPerUiUnit > 0.0f))
        {
            return;
        }
        // The paragraph's pixels in UI units, so that the root scale and the pixels per unit put them back on pixels.
        const float perPixel = 1.0f / pixelsPerUiUnit;
        const std::uint32_t packed = PainterDetail::PackColour(colour);

        const bool sdf = run.rendering == FontRendering::Sdf;
        PaintSetup setup;
        setup.image = TextureSlot2D{run.page, LinearClamp2D};
        for (const PositionedGlyph& glyph : run.glyphs)
        {
            const Rect2D rect{point.x + glyph.quad.x * perPixel, point.y + glyph.quad.y * perPixel,
                              glyph.quad.width * perPixel, glyph.quad.height * perPixel};
            if (rect.width <= 0.0f || rect.height <= 0.0f)
            {
                continue;
            }
            Quad quad;
            quad.vertex.halfSize = {rect.width * 0.5f, rect.height * 0.5f};
            quad.centre = Math::Vec2(rect.x, rect.y) + quad.vertex.halfSize;
            quad.min = -quad.vertex.halfSize;
            quad.max = quad.vertex.halfSize;
            quad.vertex.kinds[0] = static_cast<std::uint8_t>(sdf ? ShapeKind::SdfGlyph : ShapeKind::Glyph);
            quad.vertex.shape.x = run.sdfSpread;
            quad.vertex.kinds[1] = static_cast<std::uint8_t>(PaintKind::Solid);
            quad.vertex.kinds[3] = PainterImageSlot;
            quad.vertex.colour = packed;

            // The quad's UI units to the page's texels: its top-left on the region's, its size the region's.
            const ImageRegion& region = glyph.atlas.region;
            const Math::Vec2 texels{static_cast<float>(region.width) / rect.width,
                                    static_cast<float>(region.height) / rect.height};
            setup.toPaint.x = {texels.x, 0.0f};
            setup.toPaint.y = {0.0f, texels.y};
            setup.toPaint.origin = {static_cast<float>(region.x) - rect.x * texels.x,
                                    static_cast<float>(region.y) - rect.y * texels.y};
            Emit(quad, setup);
        }

        if (run.decoration == TextDecoration::None || run.width <= 0.0f)
        {
            return;
        }
        const float thickness = run.underlineThickness > 0.0f ? run.underlineThickness : 1.0f;
        float centre = run.baseline + run.underlineOffset;
        if (run.decoration == TextDecoration::LineThrough)
        {
            centre = run.baseline - run.ascent * LineThroughShareOfAscent;
        }
        else if (run.decoration == TextDecoration::Overline)
        {
            centre = run.baseline - run.ascent + thickness * 0.5f;
        }
        DrawRect({point.x + run.left * perPixel, point.y + (centre - thickness * 0.5f) * perPixel, run.width * perPixel,
                  thickness * perPixel},
                 colour);
    }
} // namespace Fenrir
