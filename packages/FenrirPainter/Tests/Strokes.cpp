// The painter's strokes by what they append to a 2D draw list (the spec's seam 2): each subpath's outline offset to
// both sides, joined and capped, filled as a path is with its fringe; a stroked circle or rounded rectangle with no
// dashes stays one analytic quad. Expected values are worked by hand from the strokes.

#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirPainter/Painter.hpp"
#include "FenrirPainter/Path.hpp"
#include "PainterTest.hpp"

using Fenrir::Colour;
using Fenrir::LineCap;
using Fenrir::LineJoin;
using Fenrir::Math::Vec2;
using Fenrir::Math::Vec4;
using PainterTest::Near;

namespace
{
    /** @brief The solid and fringe triangles of `path` stroked with `style` on a fresh painter. */
    std::vector<PainterTest::Triangle> Stroked(const Fenrir::Path& path, const Fenrir::StrokeStyle& style)
    {
        Fenrir::DrawList2D list;
        Fenrir::Painter painter(list, PainterTest::Config());
        painter.StrokePath(path, style, Colour::White);
        return PainterTest::Triangles(list);
    }

    /** @brief A corner: right along the top, then down, turning at (50, 0). */
    Fenrir::Path Corner()
    {
        Fenrir::Path path;
        path.MoveTo({0, 0}).LineTo({50, 0}).LineTo({50, 50});
        return path;
    }

    /** @brief A line along the X axis from 0 to 50. */
    Fenrir::Path Line()
    {
        Fenrir::Path path;
        path.MoveTo({0, 0}).LineTo({50, 0});
        return path;
    }

    Fenrir::StrokeStyle Style(float width, LineCap cap, LineJoin join)
    {
        Fenrir::StrokeStyle style;
        style.width = width;
        style.cap = cap;
        style.join = join;
        return style;
    }
} // namespace

TEST_CASE("A dashed stroke draws one piece for each dash", "[painter][strokes]")
{
    Fenrir::Path line;
    line.MoveTo({0, 0}).LineTo({100, 0});
    Fenrir::StrokeStyle style = Style(4, LineCap::Butt, LineJoin::Miter);
    style.dashes = {10, 10};

    const std::vector<PainterTest::Triangle> dashed = Stroked(line, style);
    INFO("five dashes along 100 units, each a rectangle of two solid triangles and a fringe of eight");
    REQUIRE(PainterTest::Count(dashed, true) == 10);
    REQUIRE(PainterTest::Count(dashed, false) == 40);
    INFO("the dashes cover 0 to 10, 20 to 30, and so on, and the gaps nothing");
    REQUIRE(PainterTest::Covered(dashed, {5, 0}));
    REQUIRE_FALSE(PainterTest::Covered(dashed, {15, 0}));
    REQUIRE(PainterTest::Covered(dashed, {85, 0}));
}

TEST_CASE("A dash offset starts the stroke that far into the pattern", "[painter][strokes]")
{
    Fenrir::Path line;
    line.MoveTo({0, 0}).LineTo({100, 0});
    Fenrir::StrokeStyle style = Style(4, LineCap::Butt, LineJoin::Miter);
    style.dashes = {10, 10};
    style.dashOffset = 5;

    const std::vector<PainterTest::Triangle> dashed = Stroked(line, style);
    INFO("five units in, the dashes run 0 to 5, 15 to 25 ... 95 to 100: six pieces");
    REQUIRE(PainterTest::Count(dashed, true) == 12);
    REQUIRE(PainterTest::Covered(dashed, {2, 0}));
    REQUIRE_FALSE(PainterTest::Covered(dashed, {10, 0}));
}

TEST_CASE("An odd dash list repeats to make an even one", "[painter][strokes]")
{
    Fenrir::Path line;
    line.MoveTo({0, 0}).LineTo({100, 0});
    Fenrir::StrokeStyle style = Style(4, LineCap::Butt, LineJoin::Miter);
    style.dashes = {10};

    INFO("{10} is {10, 10}: five dashes of two solid triangles");
    REQUIRE(PainterTest::Count(Stroked(line, style), true) == 10);
}

TEST_CASE("A dash of no length with round caps is a dot", "[painter][strokes]")
{
    Fenrir::Path line;
    line.MoveTo({0, 0}).LineTo({30, 0});
    Fenrir::StrokeStyle style = Style(6, LineCap::Round, LineJoin::Miter);
    style.dashes = {0, 10};

    const std::vector<PainterTest::Triangle> dotted = Stroked(line, style);
    INFO("a dot at 0, 10 and 20, each a disc of radius 3, and nothing between them");
    REQUIRE(PainterTest::Covered(dotted, {10, 0}));
    REQUIRE(PainterTest::Covered(dotted, {10, 2}));
    REQUIRE(PainterTest::Covered(dotted, {20, -2}));
    REQUIRE_FALSE(PainterTest::Covered(dotted, {15, 0}));
    REQUIRE_FALSE(PainterTest::Covered(dotted, {10, 3.5f}));
}

TEST_CASE("A closed subpath of two points turns at both ends with its joins", "[painter][strokes]")
{
    Fenrir::Path there;
    there.MoveTo({0, 0}).LineTo({50, 0}).Close();
    const std::vector<PainterTest::Triangle> stroke = Stroked(there, Style(10, LineCap::Butt, LineJoin::Round));
    INFO("round joins reach half the width past both ends, where butt caps would stop");
    REQUIRE(PainterTest::Covered(stroke, {-3, 0}));
    REQUIRE(PainterTest::Covered(stroke, {53, 0}));
    REQUIRE_FALSE(PainterTest::Covered(stroke, {-6, 0}));
}

TEST_CASE("A miter join meets the outer edges at a point", "[painter][strokes]")
{
    const std::vector<PainterTest::Triangle> stroke = Stroked(Corner(), Style(10, LineCap::Butt, LineJoin::Miter));
    INFO("the outer corner at (55, -5) is filled in to within a pixel");
    REQUIRE(PainterTest::Covered(stroke, {54, -4}));
    INFO("and the inside of the bend is not drawn past the stroke");
    REQUIRE_FALSE(PainterTest::Covered(stroke, {44, 6}));
    REQUIRE(PainterTest::Covered(stroke, {46, 4}));
}

TEST_CASE("A miter longer than the limit is bevelled", "[painter][strokes]")
{
    // The right angle's miter is sqrt(2) widths long.
    Fenrir::StrokeStyle style = Style(10, LineCap::Butt, LineJoin::Miter);
    style.miterLimit = 1.2f;
    const std::vector<PainterTest::Triangle> stroke = Stroked(Corner(), style);
    INFO("the corner is cut by the line from (50, -5) to (55, 0)");
    REQUIRE_FALSE(PainterTest::Covered(stroke, {54, -4}));
    REQUIRE(PainterTest::Covered(stroke, {51, -1}));
}

TEST_CASE("A bevel join cuts the outer corner straight across", "[painter][strokes]")
{
    const std::vector<PainterTest::Triangle> stroke = Stroked(Corner(), Style(10, LineCap::Butt, LineJoin::Bevel));
    INFO("the corner is cut by the line from (50, -5) to (55, 0)");
    REQUIRE_FALSE(PainterTest::Covered(stroke, {54, -4}));
    REQUIRE(PainterTest::Covered(stroke, {51, -1}));
}

TEST_CASE("A round join rounds the outer corner about the turning point", "[painter][strokes]")
{
    const std::vector<PainterTest::Triangle> stroke = Stroked(Corner(), Style(10, LineCap::Butt, LineJoin::Round));
    INFO("within the half width of (50, 0) it is covered, the square corner past it not");
    REQUIRE(PainterTest::Covered(stroke, {52.5f, -2.5f}));
    REQUIRE_FALSE(PainterTest::Covered(stroke, {54, -4}));
}

TEST_CASE("A butt cap ends the stroke at its end point", "[painter][strokes]")
{
    const std::vector<PainterTest::Triangle> stroke = Stroked(Line(), Style(10, LineCap::Butt, LineJoin::Miter));
    INFO("covered just inside the end, not just outside it");
    REQUIRE(PainterTest::Covered(stroke, {2, 3}));
    REQUIRE_FALSE(PainterTest::Covered(stroke, {-2, 0}));
    INFO("a rectangle: two solid triangles");
    REQUIRE(PainterTest::Count(stroke, true) == 2);
}

TEST_CASE("A square cap reaches half the width past the end point", "[painter][strokes]")
{
    const std::vector<PainterTest::Triangle> stroke = Stroked(Line(), Style(10, LineCap::Square, LineJoin::Miter));
    INFO("covered to x = -5, square at its corners");
    REQUIRE(PainterTest::Covered(stroke, {-3, 3}));
    REQUIRE(PainterTest::Covered(stroke, {53, -3}));
    REQUIRE_FALSE(PainterTest::Covered(stroke, {-6, 0}));
    INFO("a rectangle: two solid triangles");
    REQUIRE(PainterTest::Count(stroke, true) == 2);
}

TEST_CASE("A round cap is a half disc about the end point", "[painter][strokes]")
{
    const std::vector<PainterTest::Triangle> stroke = Stroked(Line(), Style(10, LineCap::Round, LineJoin::Miter));
    INFO("covered along the axis past the end, not in the square corner");
    REQUIRE(PainterTest::Covered(stroke, {-3, 0}));
    REQUIRE(PainterTest::Covered(stroke, {53, 0}));
    REQUIRE_FALSE(PainterTest::Covered(stroke, {-4, -4}));
    REQUIRE_FALSE(PainterTest::Covered(stroke, {-6, 0}));
}

TEST_CASE("A closed stroke covers each point once, its joins included", "[painter][strokes]")
{
    Fenrir::Path square;
    square.AddPolygon(std::vector<Vec2>{{20, 20}, {60, 20}, {60, 60}, {20, 60}});
    const std::vector<PainterTest::Triangle> stroke = Stroked(square, Style(10, LineCap::Butt, LineJoin::Miter));
    INFO("the band from 15 to 65 less 25 to 55, each edge moved half a pixel in: 49^2 - 31^2, no triangle overlapping");
    REQUIRE(Near(PainterTest::SolidArea(stroke), 49.0f * 49.0f - 31.0f * 31.0f, 0.5f));
    REQUIRE_FALSE(PainterTest::Covered(stroke, {40, 40}));
}

TEST_CASE("A stroke is the fill of its stroked outline", "[painter][strokes]")
{
    Fenrir::Path path;
    path.MoveTo({10, 10}).CubicTo({60, 0}, {0, 60}, {70, 50}).LineTo({20, 70});
    Fenrir::StrokeStyle style = Style(6, LineCap::Round, LineJoin::Round);
    style.dashes = {30, 8};

    Fenrir::DrawList2D stroked;
    Fenrir::Painter strokePainter(stroked, PainterTest::Config());
    strokePainter.StrokePath(path, style, Colour::White);

    Fenrir::DrawList2D filled;
    Fenrir::Painter fillPainter(filled, PainterTest::Config());
    fillPainter.FillPath(path.StrokeOutline(style), Colour::White);

    INFO("the same vertices and indices, byte for byte");
    REQUIRE(stroked.vertices.size() == filled.vertices.size());
    REQUIRE(std::memcmp(stroked.vertices.data(), filled.vertices.data(), stroked.vertices.size()) == 0);
    REQUIRE(stroked.indices == filled.indices);
    REQUIRE(PainterTest::Draw(stroked, 0).indexCount == PainterTest::Draw(filled, 0).indexCount);
}

TEST_CASE("A hairline is one pixel wide whatever the transform", "[painter][strokes]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());
    painter.Scale({4, 4});
    painter.StrokePath(Line(), Style(0, LineCap::Butt, LineJoin::Miter), Colour::White);

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    INFO("the hairline draws");
    REQUIRE_FALSE(vertices.empty());
    for (const Fenrir::PainterVertex& vertex : vertices)
    {
        INFO("half a pixel each side of the line, the fringe running from its middle to a pixel out");
        REQUIRE((Near(vertex.position.y, 0.0f) || Near(std::abs(vertex.position.y), 1.0f)));
        REQUIRE(Near(std::abs(vertex.position.y), 1.0f - vertex.local.x));
    }
}

TEST_CASE("A stroked circle with no dashes is one analytic ring", "[painter][strokes]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());
    Fenrir::Path circle;
    circle.AddCircle({50, 50}, 20);
    painter.StrokePath(circle, Style(4, LineCap::Butt, LineJoin::Miter), Colour::White);

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    INFO("one quad of kind Ring, from radius 22 in to 18");
    REQUIRE(vertices.size() == 4);
    REQUIRE(PainterTest::ShapeOf(vertices[0]) == Fenrir::ShapeKind::Ring);
    REQUIRE(Near(vertices[0].shape, Vec4(22, 18, 0, 0)));
}

TEST_CASE("A stroked rounded rectangle in a colour with no dashes is one analytic border", "[painter][strokes]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());
    Fenrir::Path box;
    box.AddRoundedRect({10, 10, 80, 40}, Fenrir::CornerRadii::All(6));
    painter.StrokePath(box, Style(4, LineCap::Butt, LineJoin::Miter), Colour::Red);

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    INFO("one quad of kind Border over the rectangle grown by half the width, radii 8, sides 4 wide, all red");
    REQUIRE(vertices.size() == 4);
    REQUIRE(PainterTest::ShapeOf(vertices[0]) == Fenrir::ShapeKind::Border);
    REQUIRE(Near(vertices[0].halfSize, Vec2(42, 22)));
    REQUIRE(Near(vertices[0].radii, Vec4(8)));
    REQUIRE(Near(vertices[0].shape, Vec4(4)));
    REQUIRE(vertices[0].borderColours[0] == PainterTest::Rgba8(255, 0, 0, 255));
}

TEST_CASE("A stroke thinner than a pixel is a pixel wide, its colour faded to its width", "[painter][strokes]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());
    painter.StrokePath(Line(), Style(0.5f, LineCap::Butt, LineJoin::Miter), Colour::White);

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    INFO("the stroke draws");
    REQUIRE_FALSE(vertices.empty());
    for (const Fenrir::PainterVertex& vertex : vertices)
    {
        INFO("a hairline's vertices, half a pixel each side of the line and the fringe a pixel out");
        REQUIRE((Near(vertex.position.y, 0.0f) || Near(std::abs(vertex.position.y), 1.0f)));
        INFO("white at half alpha, premultiplied: 128 in every channel");
        REQUIRE(vertex.colour == PainterTest::Rgba8(128, 128, 128, 128));
    }
}

TEST_CASE("A dashed circle is tessellated", "[painter][strokes]")
{
    Fenrir::Path circle;
    circle.AddCircle({50, 50}, 20);
    Fenrir::StrokeStyle dashed = Style(4, LineCap::Butt, LineJoin::Miter);
    dashed.dashes = {5, 5};
    INFO("a dashed circle is triangles");
    REQUIRE_FALSE(Stroked(circle, dashed).empty());
}

TEST_CASE("A rectangle whose corners would bevel is tessellated", "[painter][strokes]")
{
    Fenrir::Path box;
    box.AddRect({10, 10, 80, 40});
    INFO("a rectangle with bevelled corners is triangles, cut at its corners");
    const std::vector<PainterTest::Triangle> bevelled = Stroked(box, Style(4, LineCap::Butt, LineJoin::Bevel));
    REQUIRE_FALSE(bevelled.empty());
    REQUIRE_FALSE(PainterTest::Covered(bevelled, {8.5f, 8.5f}));
}
