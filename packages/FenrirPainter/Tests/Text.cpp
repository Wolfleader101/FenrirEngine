// The painter's text by what it appends to a 2D draw list (the spec's seam 2): a paragraph's or glyph run's glyphs as
// quads of shape kind Glyph over their atlas rectangles, the page in the image slot and the text colour as the paint,
// under the painter's transform and clip; a shadow before its text; a decoration as a rectangle from the run's metrics;
// an SDF face's glyphs as quads of shape kind SdfGlyph, carrying the field's spread.
// Paragraphs are laid out in the embedded default face and HarfBuzz's Roboto, through FenrirText's test fixture.

#include <cstddef>
#include <numbers>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirPainter/Painter.hpp"
#include "FenrirText/Paragraph.hpp"
#include "PainterTest.hpp"
#include "TextTest.hpp"

using Fenrir::Colour;
using Fenrir::Rect2D;
using Fenrir::Math::Vec2;
using PainterTest::Near;

namespace
{
    /** @brief Every glyph of the paragraph, run by run, which is the order the painter draws them in. */
    std::size_t GlyphCount(const Fenrir::Paragraph& paragraph)
    {
        std::size_t count = 0;
        for (const Fenrir::GlyphRun& run : paragraph.Runs())
        {
            count += run.glyphs.size();
        }
        return count;
    }

    /** @brief `text` in the default face at `size` pixels and `colour`, laid out with no width limit. */
    Fenrir::Paragraph LaidOut(TextTest::TextApp& test, std::string_view text, float size = 16.0f,
                              const Colour& colour = Colour::White)
    {
        Fenrir::Paragraph paragraph;
        Fenrir::TextStyle style = TextTest::Style("default", size);
        style.colour = colour;
        paragraph.SetText(text, style);
        paragraph.Layout(test.fonts, Fenrir::UnlimitedWidth);
        return paragraph;
    }

    /** @brief The four corners of `rect`, in the order the painter emits a quad's vertices. */
    std::vector<Vec2> Corners(const Rect2D& rect)
    {
        return {{rect.x, rect.y},
                {rect.x + rect.width, rect.y},
                {rect.x + rect.width, rect.y + rect.height},
                {rect.x, rect.y + rect.height}};
    }

    /** @brief The four corners of an atlas region, in texels. */
    std::vector<Vec2> Corners(const Fenrir::ImageRegion& region)
    {
        return Corners(Rect2D{static_cast<float>(region.x), static_cast<float>(region.y),
                              static_cast<float>(region.width), static_cast<float>(region.height)});
    }

    /** @brief Whether the glyph quad starting at vertex `first` is of `kind`, covers `rect` and samples `region`. */
    bool QuadIs(const std::vector<Fenrir::PainterVertex>& vertices, std::size_t first, const Rect2D& rect,
                const Fenrir::ImageRegion& region, Fenrir::ShapeKind kind = Fenrir::ShapeKind::Glyph)
    {
        const std::vector<Vec2> positions = Corners(rect);
        const std::vector<Vec2> uvs = Corners(region);
        for (std::size_t i = 0; i < 4; ++i)
        {
            const Fenrir::PainterVertex& vertex = vertices[first + i];
            if (PainterTest::ShapeOf(vertex) != kind || !Near(vertex.position, positions[i]) ||
                !Near(vertex.uv, uvs[i]))
            {
                return false;
            }
        }
        return true;
    }

    /** @brief The centre of the quad starting at vertex `first`: the mean of its opposite corners. */
    Vec2 LineCentre(const std::vector<Fenrir::PainterVertex>& vertices, std::size_t first)
    {
        return (vertices[first].position + vertices[first + 2].position) * 0.5f;
    }

    /** @brief The glyph's quad moved by `offset`. */
    Rect2D Moved(const Rect2D& rect, Vec2 offset)
    {
        return {rect.x + offset.x, rect.y + offset.y, rect.width, rect.height};
    }
} // namespace

TEST_CASE("A paragraph of N visible glyphs is N glyph quads over their atlas rectangles", "[painter][text]")
{
    TextTest::TextApp test;
    Fenrir::Paragraph paragraph = LaidOut(test, "Hi there", 16.0f, Colour::Red);
    const std::size_t glyphs = GlyphCount(paragraph);
    INFO("the space has nothing to draw, so seven glyphs");
    REQUIRE(glyphs == 7);

    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());
    painter.DrawText(paragraph, {10, 20});

    INFO("one command of seven quads: four vertices and six indices a glyph");
    REQUIRE(list.commands.size() == 1);
    const Fenrir::DrawCommand2D& command = PainterTest::Draw(list, 0);
    REQUIRE(command.indexCount == glyphs * 6);
    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    REQUIRE(vertices.size() == glyphs * 4);

    const Fenrir::GlyphRun& run = paragraph.Runs().front();
    INFO("the page is in the image slot, the text colour the solid paint");
    REQUIRE(command.textures.size() > Fenrir::PainterImageSlot);
    REQUIRE(command.textures[Fenrir::PainterImageSlot].image == run.page);
    REQUIRE(PainterTest::PaintOf(vertices[0]) == Fenrir::PaintKind::Solid);
    REQUIRE(vertices[0].colour == PainterTest::Rgba8(255, 0, 0, 255));

    INFO("each glyph's quad is its whole-pixel rectangle from the point, sampling its atlas rectangle in texels");
    std::size_t first = 0;
    for (const Fenrir::GlyphRun& each : paragraph.Runs())
    {
        for (const Fenrir::PositionedGlyph& glyph : each.glyphs)
        {
            REQUIRE(QuadIs(vertices, first, Moved(glyph.quad, {10, 20}), glyph.atlas.region));
            first += 4;
        }
    }
}

TEST_CASE("A glyph run draws alone at a point", "[painter][text]")
{
    TextTest::TextApp test;
    Fenrir::Paragraph paragraph = LaidOut(test, "Run");
    const Fenrir::GlyphRun& run = paragraph.Runs().front();

    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());
    painter.DrawGlyphs(run, {5, 6});

    INFO("a quad a glyph of the run, the first over its rectangle from the point");
    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    REQUIRE(vertices.size() == run.glyphs.size() * 4);
    REQUIRE(QuadIs(vertices, 0, Moved(run.glyphs.front().quad, {5, 6}), run.glyphs.front().atlas.region));
}

TEST_CASE("Glyphs of two faces are two commands, each with its own page", "[painter][text]")
{
    TextTest::TextApp test;
    test.Register("Roboto", TextTest::FullFont("Roboto-Regular.ttf"));
    Fenrir::Paragraph paragraph;
    paragraph.AddRun("Ab", TextTest::Style("default"));
    paragraph.AddRun("Cd", TextTest::Style("Roboto"));
    paragraph.Layout(test.fonts, Fenrir::UnlimitedWidth);
    INFO("each face has its own run and atlas page");
    REQUIRE(paragraph.Runs().size() == 2);
    REQUIRE(paragraph.Runs()[0].page != paragraph.Runs()[1].page);

    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());
    painter.DrawText(paragraph, {0, 0});

    INFO("the second page cannot share the first's image slot, so it starts a second command");
    REQUIRE(list.commands.size() == 2);
    REQUIRE(PainterTest::Draw(list, 0).textures[Fenrir::PainterImageSlot].image == paragraph.Runs()[0].page);
    REQUIRE(PainterTest::Draw(list, 1).textures[Fenrir::PainterImageSlot].image == paragraph.Runs()[1].page);
}

TEST_CASE("Text under a rotated transform lands rotated", "[painter][text]")
{
    TextTest::TextApp test;
    Fenrir::Paragraph paragraph = LaidOut(test, "T");
    const Fenrir::PositionedGlyph& glyph = paragraph.Runs().front().glyphs.front();

    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());
    painter.Translate({100, 100});
    painter.Rotate(std::numbers::pi_v<float> / 2.0f);
    painter.DrawText(paragraph, {0, 0});

    // A quarter turn clockwise on screen takes (x, y) to (-y, x).
    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    const Vec2 corner{glyph.quad.x, glyph.quad.y};
    INFO("the glyph's top-left corner turned about the translated origin");
    REQUIRE(Near(vertices[0].position, Vec2(100 - corner.y, 100 + corner.x), 1e-3f));
}

TEST_CASE("Text under a clip carries the clip and its scissor", "[painter][text]")
{
    TextTest::TextApp test;
    Fenrir::Paragraph paragraph = LaidOut(test, "Clip");

    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());
    painter.ClipRoundedRect({0, 0, 20, 40}, Fenrir::CornerRadii::All(4));
    painter.DrawText(paragraph, {2, 2});

    INFO("the command is scissored and every glyph vertex carries the rounded clip");
    REQUIRE(PainterTest::Draw(list, 0).scissor.has_value());
    for (const Fenrir::PainterVertex& vertex : PainterTest::Vertices(list))
    {
        REQUIRE(PainterTest::HasFlag(vertex, Fenrir::PainterFlags::Clipped));
        REQUIRE(Near(vertex.clipHalfSize, Vec2(10, 20)));
        REQUIRE(Near(vertex.clipRadii, Fenrir::Math::Vec4(4)));
    }
}

TEST_CASE("A text shadow draws offset and tinted before its text", "[painter][text]")
{
    TextTest::TextApp test;
    Fenrir::Paragraph paragraph = LaidOut(test, "Shade", 16.0f, Colour::White);
    const std::size_t glyphs = GlyphCount(paragraph);
    const Fenrir::PositionedGlyph& glyph = paragraph.Runs().front().glyphs.front();

    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());
    painter.DrawText(paragraph, {10, 10}, Fenrir::TextShadow{{2, 3}, Colour::Black});

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    REQUIRE(vertices.size() == glyphs * 8);
    INFO("the shadow's glyphs first, moved by its offset, in its colour");
    REQUIRE(QuadIs(vertices, 0, Moved(glyph.quad, {12, 13}), glyph.atlas.region));
    REQUIRE(vertices[0].colour == PainterTest::Rgba8(0, 0, 0, 255));
    INFO("then the text's, where they belong, in the text's colour");
    REQUIRE(QuadIs(vertices, glyphs * 4, Moved(glyph.quad, {10, 10}), glyph.atlas.region));
    REQUIRE(vertices[glyphs * 4].colour == PainterTest::Rgba8(255, 255, 255, 255));
}

TEST_CASE("An underline is a rectangle under the run, from its metrics", "[painter][text]")
{
    TextTest::TextApp test;
    Fenrir::Paragraph paragraph;
    Fenrir::TextStyle style = TextTest::Style("default");
    style.decoration = Fenrir::TextDecoration::Underline;
    paragraph.SetText("Under", style);
    paragraph.Layout(test.fonts, Fenrir::UnlimitedWidth);
    const Fenrir::GlyphRun& run = paragraph.Runs().front();

    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());
    painter.DrawText(paragraph, {0, 0});

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    INFO("the glyphs, then one rectangle quad after them");
    REQUIRE(vertices.size() == (run.glyphs.size() + 1) * 4);
    const Fenrir::PainterVertex& line = vertices[run.glyphs.size() * 4];
    REQUIRE(PainterTest::ShapeOf(line) == Fenrir::ShapeKind::Rectangle);
    INFO("the rectangle spans the run's advances, centred the underline offset below the baseline");
    REQUIRE(Near(line.halfSize, Vec2(run.width * 0.5f, run.underlineThickness * 0.5f)));
    REQUIRE(Near(LineCentre(vertices, run.glyphs.size() * 4),
                 Vec2(run.left + run.width * 0.5f, run.baseline + run.underlineOffset), 1e-3f));
}

TEST_CASE("A paragraph laid out at the root's scale lands on whole pixels of the target", "[painter][text]")
{
    // At root scale 2 and two pixels per logical unit, a 10-unit font is laid out at 40 pixels.
    TextTest::TextApp test;
    Fenrir::Paragraph paragraph = LaidOut(test, "Scaled", 40.0f);
    const Fenrir::PositionedGlyph& glyph = paragraph.Runs().front().glyphs.front();

    Fenrir::DrawList2D list;
    Fenrir::PainterConfig config = PainterTest::Config();
    config.scale = 2.0f;
    config.pixelsPerUnit = 2.0f;
    Fenrir::Painter painter(list, config);
    painter.DrawText(paragraph, {10, 20});

    // The point is (20, 40) logical units; each pixel of the paragraph is half a logical unit.
    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    INFO("the glyph's quad is its pixel rectangle at half a logical unit a pixel, from the scaled point");
    REQUIRE(Near(vertices[0].position, Vec2(20 + glyph.quad.x * 0.5f, 40 + glyph.quad.y * 0.5f)));
    REQUIRE(Near(vertices[2].position,
                 Vec2(20 + (glyph.quad.x + glyph.quad.width) * 0.5f, 40 + (glyph.quad.y + glyph.quad.height) * 0.5f)));
}

TEST_CASE("With pixel snapping the text's origin moves to a whole pixel", "[painter][text]")
{
    TextTest::TextApp test;
    Fenrir::Paragraph paragraph = LaidOut(test, "Snap");
    const Fenrir::PositionedGlyph& glyph = paragraph.Runs().front().glyphs.front();

    Fenrir::DrawList2D list;
    Fenrir::PainterConfig config = PainterTest::Config();
    config.snapToPixels = true;
    Fenrir::Painter painter(list, config);
    painter.DrawText(paragraph, {10.3f, 20.6f});

    INFO("the point (10.3, 20.6) rounds to (10, 21), and the glyph's whole-pixel quad is drawn from there");
    REQUIRE(Near(PainterTest::Vertices(list)[0].position, Vec2(10 + glyph.quad.x, 21 + glyph.quad.y)));
}

TEST_CASE("A paragraph not yet laid out is laid out on the spot, rasterising its glyphs", "[painter][text]")
{
    TextTest::TextApp test;
    Fenrir::Paragraph paragraph;
    paragraph.SetText("Fresh", TextTest::Style("default", 23.0f));
    REQUIRE(test.fonts.AtlasPages(test.fonts.DefaultFont(), 23.0f).empty());

    Fenrir::DrawList2D list;
    Fenrir::PainterConfig config = PainterTest::Config();
    config.fonts = &test.fonts;
    Fenrir::Painter painter(list, config);
    painter.DrawText(paragraph, {0, 0});

    INFO("the atlas at 23 pixels was made");
    REQUIRE_FALSE(test.fonts.AtlasPages(test.fonts.DefaultFont(), 23.0f).empty());
    INFO("and the five glyphs drawn from it");
    REQUIRE(PainterTest::Vertices(list).size() == 5 * 4);
}

TEST_CASE("A line-through is a rectangle across the run above its baseline, and an overline at its ascent",
          "[painter][text]")
{
    TextTest::TextApp test;
    for (const Fenrir::TextDecoration decoration :
         {Fenrir::TextDecoration::LineThrough, Fenrir::TextDecoration::Overline})
    {
        Fenrir::Paragraph paragraph;
        Fenrir::TextStyle style = TextTest::Style("default");
        style.decoration = decoration;
        paragraph.SetText("Line", style);
        paragraph.Layout(test.fonts, Fenrir::UnlimitedWidth);
        const Fenrir::GlyphRun& run = paragraph.Runs().front();

        Fenrir::DrawList2D list;
        Fenrir::Painter painter(list, PainterTest::Config());
        painter.DrawText(paragraph, {0, 0});

        const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
        INFO("one rectangle after the glyphs, across the run's advances");
        REQUIRE(vertices.size() == (run.glyphs.size() + 1) * 4);
        const Vec2 centre = LineCentre(vertices, run.glyphs.size() * 4);
        REQUIRE(Near(centre.x, run.left + run.width * 0.5f, 1e-3f));
        if (decoration == Fenrir::TextDecoration::LineThrough)
        {
            INFO("a line-through sits above the baseline, below half the ascent");
            REQUIRE(centre.y < run.baseline);
            REQUIRE(centre.y > run.baseline - run.ascent * 0.5f);
        }
        else
        {
            INFO("an overline's top is at the ascent");
            REQUIRE(Near(centre.y, run.baseline - run.ascent + run.underlineThickness * 0.5f, 1e-3f));
        }
    }
}

TEST_CASE("A glyph run's shadow draws offset and tinted before it", "[painter][text]")
{
    TextTest::TextApp test;
    Fenrir::Paragraph paragraph = LaidOut(test, "Run");
    const Fenrir::GlyphRun& run = paragraph.Runs().front();

    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());
    painter.DrawGlyphs(run, {0, 0}, Fenrir::TextShadow{{1, 1}, Colour::Black});

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    INFO("the shadow's quads first, moved by (1, 1) and black, then the run's");
    REQUIRE(vertices.size() == run.glyphs.size() * 8);
    REQUIRE(QuadIs(vertices, 0, Moved(run.glyphs.front().quad, {1, 1}), run.glyphs.front().atlas.region));
    REQUIRE(vertices[0].colour == PainterTest::Rgba8(0, 0, 0, 255));
    REQUIRE(QuadIs(vertices, run.glyphs.size() * 4, run.glyphs.front().quad, run.glyphs.front().atlas.region));
}

TEST_CASE("An SDF face's glyphs are SdfGlyph quads over its SDF atlas, carrying its spread", "[painter][text][sdf]")
{
    TextTest::TextApp test;
    const Fenrir::AssetHandle<Fenrir::Font> roboto =
        test.store.Load<Fenrir::Font>(TextTest::FullFont("Roboto-Regular.ttf").string(), Fenrir::AssetOwner::App())
            .handle;
    test.fonts.AddFace("Field", roboto, Fenrir::FontWeightNormal, Fenrir::FontStyle::Normal,
                       Fenrir::FontRendering::Sdf);
    Fenrir::Paragraph paragraph;
    paragraph.AddRun("Far", TextTest::Style("Field", 21.0f));
    paragraph.AddRun("near", TextTest::Style("default", 21.0f));
    paragraph.Layout(test.fonts, Fenrir::UnlimitedWidth);
    REQUIRE(paragraph.Runs().size() == 2);
    const Fenrir::GlyphRun& field = paragraph.Runs()[0];
    REQUIRE(field.rendering == Fenrir::FontRendering::Sdf);

    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());
    painter.DrawText(paragraph, {10, 20});

    INFO("the SDF run is a command of its own over the font's SDF atlas page, then the bitmap run over its page");
    REQUIRE(list.commands.size() == 2);
    REQUIRE(PainterTest::Draw(list, 0).textures[Fenrir::PainterImageSlot].image ==
            test.fonts.AtlasPages(roboto, 0.0f, Fenrir::FontRendering::Sdf).front());
    REQUIRE(PainterTest::Draw(list, 1).textures[Fenrir::PainterImageSlot].image == paragraph.Runs()[1].page);

    INFO(
        "each SDF glyph is an SdfGlyph quad over its fractional rectangle, sampling its region, the spread in shape.x");
    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    std::size_t first = 0;
    for (const Fenrir::PositionedGlyph& glyph : field.glyphs)
    {
        REQUIRE(QuadIs(vertices, first, Moved(glyph.quad, {10, 20}), glyph.atlas.region, Fenrir::ShapeKind::SdfGlyph));
        REQUIRE(vertices[first].shape.x == field.sdfSpread);
        first += 4;
    }
    INFO("and the bitmap run's glyphs stay Glyph quads");
    REQUIRE(PainterTest::ShapeOf(vertices[first]) == Fenrir::ShapeKind::Glyph);
}

TEST_CASE("Under pixel snapping an SDF run draws at its fractional point and a bitmap run at a whole pixel",
          "[painter][text][sdf]")
{
    TextTest::TextApp test;
    test.fonts.AddFace("Field", test.fonts.DefaultFont(), Fenrir::FontWeightNormal, Fenrir::FontStyle::Normal,
                       Fenrir::FontRendering::Sdf);
    Fenrir::Paragraph field;
    field.SetText("I", TextTest::Style("Field", 16.0f));
    field.Layout(test.fonts, Fenrir::UnlimitedWidth);
    Fenrir::Paragraph crisp = LaidOut(test, "I");

    Fenrir::PainterConfig config = PainterTest::Config();
    config.snapToPixels = true;
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, config);
    painter.DrawText(field, {10.3f, 20.6f});
    painter.DrawText(crisp, {10.3f, 20.6f});

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    REQUIRE(vertices.size() == 8);
    INFO("the SDF glyph is moved by the point as given, so text moving smoothly does not step");
    const Fenrir::PositionedGlyph& sdf = field.Runs().front().glyphs.front();
    REQUIRE(QuadIs(vertices, 0, Moved(sdf.quad, {10.3f, 20.6f}), sdf.atlas.region, Fenrir::ShapeKind::SdfGlyph));
    INFO("the bitmap glyph's point is snapped to (10, 21), its whole-pixel rectangle on whole pixels");
    const Fenrir::PositionedGlyph& bitmap = crisp.Runs().front().glyphs.front();
    REQUIRE(QuadIs(vertices, 4, Moved(bitmap.quad, {10.0f, 21.0f}), bitmap.atlas.region));
}
