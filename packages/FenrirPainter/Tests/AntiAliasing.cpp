// The painter's anti-aliasing switch by what it appends to a 2D draw list (the spec's seam 2; ADR-0063's "each
// technique is a switch"): off, a path's fill and a stroke are their solid triangles with no fringe ring, a stroke
// thinner than a pixel is not faded, and every shape's vertices carry the hard-edge flag the shader resolves coverage
// by; on, the default, nothing of that changes.

#include <cmath>
#include <cstddef>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirPainter/Painter.hpp"
#include "FenrirPainter/Path.hpp"
#include "PainterTest.hpp"

using Fenrir::Colour;
using Fenrir::Math::Vec2;

namespace
{
    /** @brief The stand-in configuration with anti-aliasing off. */
    Fenrir::PainterConfig Aliased()
    {
        Fenrir::PainterConfig config = PainterTest::Config();
        config.antiAliasing = false;
        return config;
    }

    /** @brief The square from (10, 10) to (30, 30). */
    Fenrir::Path Square()
    {
        Fenrir::Path square;
        square.MoveTo({10, 10}).LineTo({30, 10}).LineTo({30, 30}).LineTo({10, 30}).Close();
        return square;
    }

    /** @brief One of each analytic shape, an image among them, then a path's fill, all under a rounded clip. */
    void DrawEveryShape(Fenrir::Painter& painter)
    {
        painter.ClipRoundedRect({0, 0, 200, 200}, Fenrir::CornerRadii::All(8));
        painter.DrawRect({10, 10, 20, 20}, Colour::Red);
        painter.DrawRoundedRect({40, 10, 20, 20}, Fenrir::CornerRadii::All(4), Colour::Red);
        painter.DrawBorder({70, 10, 20, 20}, Fenrir::CornerRadii::All(4), Fenrir::Border::Uniform(2, Colour::Red));
        Fenrir::Shadow shadow;
        shadow.blur = 4;
        painter.DrawShadow({100, 10, 20, 20}, {}, shadow);
        painter.DrawCircle({20, 60}, 10, Colour::Red);
        painter.DrawArc({50, 60}, 10, 0.0f, 1.0f, 2, Colour::Red);
        painter.DrawRing({80, 60}, 10, 6, Colour::Red);
        Fenrir::ImageDraw image;
        image.image = Fenrir::AssetHandle<Fenrir::Image>{3, 1};
        painter.DrawImage({100, 50, 20, 20}, image);
        painter.FillPath(Square(), Colour::Red);
    }
} // namespace

TEST_CASE("Anti-aliasing is on by default", "[painter][anti-aliasing]")
{
    INFO("a default PainterConfig has anti-aliasing off");
    REQUIRE(Fenrir::PainterConfig{}.antiAliasing);
}

TEST_CASE("With anti-aliasing off a filled path is its solid triangles with no fringe", "[painter][anti-aliasing]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, Aliased());

    painter.FillPath(Square(), Colour::Red);

    const std::vector<PainterTest::Triangle> triangles = PainterTest::Triangles(list);
    INFO("not two solid triangles over the square's four corners in six indices, with no fringe triangle");
    REQUIRE(PainterTest::Draw(list, 0).indexCount == 6);
    REQUIRE(PainterTest::Vertices(list).size() == 4);
    REQUIRE(PainterTest::Count(triangles, true) == 2);
    REQUIRE(PainterTest::Count(triangles, false) == 0);
    INFO("a corner was moved off the path's own, as half a fringe in would move it");
    for (const Fenrir::PainterVertex& vertex : PainterTest::Vertices(list))
    {
        REQUIRE((vertex.position.x == 10.0f || vertex.position.x == 30.0f));
        REQUIRE((vertex.position.y == 10.0f || vertex.position.y == 30.0f));
    }
    REQUIRE(PainterTest::SolidArea(triangles) == 400.0f);
}

TEST_CASE("With anti-aliasing off a stroke has no fringe", "[painter][anti-aliasing]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, Aliased());
    Fenrir::Path corner;
    corner.MoveTo({0, 0}).LineTo({50, 0}).LineTo({50, 50});
    Fenrir::StrokeStyle style;
    style.width = 4;

    painter.StrokePath(corner, style, Colour::White);

    const std::vector<PainterTest::Triangle> triangles = PainterTest::Triangles(list);
    INFO("the stroke drew no solid triangle");
    REQUIRE(PainterTest::Count(triangles, true) > 0);
    INFO("the stroke drew fringe triangles");
    REQUIRE(PainterTest::Count(triangles, false) == 0);
    INFO("the solid triangles miss the line they stroke");
    REQUIRE(PainterTest::Covered(triangles, {25, 1}));
    REQUIRE(PainterTest::Covered(triangles, {49, 25}));
}

TEST_CASE("With anti-aliasing off a stroke thinner than a pixel is a pixel wide and not faded",
          "[painter][anti-aliasing]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, Aliased());
    Fenrir::Path line;
    line.MoveTo({0, 0}).LineTo({50, 0});
    Fenrir::StrokeStyle style;
    style.width = 0.5f;

    painter.StrokePath(line, style, Colour::White);

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    INFO("the stroke drew nothing");
    REQUIRE_FALSE(vertices.empty());
    for (const Fenrir::PainterVertex& vertex : vertices)
    {
        INFO("a vertex is not half a pixel from the line, as a hairline's with no fringe are");
        REQUIRE(PainterTest::Near(std::abs(vertex.position.y), 0.5f));
        INFO("its colour was faded from opaque white");
        REQUIRE(vertex.colour == PainterTest::Rgba8(255, 255, 255, 255));
    }
}

TEST_CASE("With anti-aliasing off every shape's vertices carry the hard-edge flag", "[painter][anti-aliasing]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, Aliased());

    DrawEveryShape(painter);

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    INFO("fewer vertices than the eight quads, nine-slice aside, and the path");
    REQUIRE(vertices.size() > 4 * 8);
    for (const Fenrir::PainterVertex& vertex : vertices)
    {
        INFO("a vertex of shape kind " << static_cast<int>(vertex.kinds[0]) << " lacks the hard-edge flag");
        REQUIRE(PainterTest::HasFlag(vertex, Fenrir::PainterFlags::HardEdge));
    }
}

TEST_CASE("With anti-aliasing on no vertex carries the hard-edge flag", "[painter][anti-aliasing]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());

    DrawEveryShape(painter);

    for (const Fenrir::PainterVertex& vertex : PainterTest::Vertices(list))
    {
        INFO("a vertex of shape kind " << static_cast<int>(vertex.kinds[0]) << " carries the hard-edge flag");
        REQUIRE_FALSE(PainterTest::HasFlag(vertex, Fenrir::PainterFlags::HardEdge));
    }
}

TEST_CASE("With anti-aliasing on a filled path keeps its fringe", "[painter][anti-aliasing]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());

    painter.FillPath(Square(), Colour::Red);

    const std::vector<PainterTest::Triangle> triangles = PainterTest::Triangles(list);
    INFO("the square is not two solid triangles inside a fringe of eight, as before the switch");
    REQUIRE(PainterTest::Count(triangles, true) == 2);
    REQUIRE(PainterTest::Count(triangles, false) == 8);
}
