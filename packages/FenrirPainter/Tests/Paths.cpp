// The painter's path fills by what they append to a 2D draw list (the spec's seam 2): flattened, split by winding,
// ear-clipped into solid triangles of shape kind Triangle, with a one-pixel fringe ring whose outer vertices have no
// coverage. Expected values are worked by hand from the paths.

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirPainter/Painter.hpp"
#include "FenrirPainter/Path.hpp"
#include "PainterTest.hpp"

using Fenrir::Colour;
using Fenrir::Math::Vec2;
using PainterTest::Near;

namespace
{
    /** @brief The distance from `point` to the segment from `a` to `b`. */
    float DistanceToSegment(Vec2 point, Vec2 a, Vec2 b)
    {
        const Vec2 ab = b - a;
        const float length2 = ab.x * ab.x + ab.y * ab.y;
        float t = length2 > 0.0f ? ((point.x - a.x) * ab.x + (point.y - a.y) * ab.y) / length2 : 0.0f;
        t = std::fmax(0.0f, std::fmin(1.0f, t));
        const Vec2 nearest = a + ab * t;
        return std::hypot(point.x - nearest.x, point.y - nearest.y);
    }

    /** @brief The distance from `point` to the nearest edge of the closed polygons `polygons`. */
    float DistanceToOutline(Vec2 point, const std::vector<std::vector<Vec2>>& polygons)
    {
        float nearest = 1e9f;
        for (const std::vector<Vec2>& polygon : polygons)
        {
            for (std::size_t i = 0; i < polygon.size(); ++i)
            {
                nearest = std::fmin(nearest, DistanceToSegment(point, polygon[i], polygon[(i + 1) % polygon.size()]));
            }
        }
        return nearest;
    }

    /** @brief The pentagram of `radius` about `centre`: each point joined to the second one on, crossing itself. */
    Fenrir::Path Pentagram(Vec2 centre, float radius)
    {
        std::array<Vec2, 5> points{};
        for (std::size_t i = 0; i < 5; ++i)
        {
            const float angle =
                -std::numbers::pi_v<float> / 2.0f + static_cast<float>(i * 2) * 2.0f * std::numbers::pi_v<float> / 5.0f;
            points[i] = centre + Vec2(std::cos(angle), std::sin(angle)) * radius;
        }
        Fenrir::Path path;
        path.AddPolygon(points);
        return path;
    }
} // namespace

TEST_CASE("A filled square path is two solid triangles inside a fringe of eight", "[painter][paths]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());
    Fenrir::Path square;
    square.MoveTo({10, 10}).LineTo({30, 10}).LineTo({30, 30}).LineTo({10, 30}).Close();

    painter.FillPath(square, Colour::Red);

    const std::vector<PainterTest::Triangle> triangles = PainterTest::Triangles(list);
    INFO("one command of thirty indices over eight vertices: each corner inside and outside the edge");
    REQUIRE(list.commands.size() == 1);
    REQUIRE(PainterTest::Draw(list, 0).indexCount == 30);
    REQUIRE(PainterTest::Vertices(list).size() == 8);
    INFO("two solid triangles and a fringe of two triangles along each of the four edges");
    REQUIRE(PainterTest::Count(triangles, true) == 2);
    REQUIRE(PainterTest::Count(triangles, false) == 8);
    for (const Fenrir::PainterVertex& vertex : PainterTest::Vertices(list))
    {
        INFO(
            "every vertex is a solid red triangle vertex, fully covered half a pixel in or uncovered half a pixel out");
        REQUIRE(PainterTest::ShapeOf(vertex) == Fenrir::ShapeKind::Triangle);
        REQUIRE(vertex.colour == PainterTest::Rgba8(255, 0, 0, 255));
        const bool inner = vertex.local.x == 1.0f;
        REQUIRE((inner || vertex.local.x == 0.0f));
        const float offset = inner ? 0.5f : -0.5f;
        REQUIRE((Near(vertex.position.x, 10.0f + offset) || Near(vertex.position.x, 30.0f - offset)));
        REQUIRE((Near(vertex.position.y, 10.0f + offset) || Near(vertex.position.y, 30.0f - offset)));
    }
    INFO("the solid triangles cover the square shrunk by half a pixel");
    REQUIRE(Near(PainterTest::SolidArea(triangles), 19.0f * 19.0f, 1e-2f));
}

TEST_CASE("A concave path with a hole is ear-clipped round the hole, its fringe following the outline",
          "[painter][paths]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());
    const std::vector<Vec2> ell{{0, 0}, {60, 0}, {60, 20}, {20, 20}, {20, 60}, {0, 60}};
    const std::vector<Vec2> hole{{5, 5}, {15, 5}, {15, 15}, {5, 15}};
    Fenrir::Path path;
    path.AddPolygon(ell).AddPolygon(hole).SetFillRule(Fenrir::FillRule::EvenOdd);

    painter.FillPath(path, Colour::White);

    const std::vector<PainterTest::Triangle> triangles = PainterTest::Triangles(list);
    // Ten vertices and one hole bridged in: 10 + 2 * 1 - 2 = 10 triangles by ear clipping.
    const std::size_t solid = PainterTest::Count(triangles, true);
    INFO("about ten solid triangles (" << solid << ")");
    REQUIRE(solid >= 9);
    REQUIRE(solid <= 12);
    INFO("a fringe of two triangles along each of the ten edges");
    REQUIRE(PainterTest::Count(triangles, false) == 20);
    INFO("the L is covered, the hole and the L's notch are not");
    REQUIRE(PainterTest::Covered(triangles, {40, 10}));
    REQUIRE(PainterTest::Covered(triangles, {10, 40}));
    REQUIRE(PainterTest::Covered(triangles, {2, 2}));
    REQUIRE_FALSE(PainterTest::Covered(triangles, {10, 10}));
    REQUIRE_FALSE(PainterTest::Covered(triangles, {40, 40}));
    INFO("the solid area is the L's less the hole's, each edge moved half a pixel in");
    // The L shrunk by half a pixel: 59 x 19 + 19 x 40; the hole grown by it: 11 x 11.
    REQUIRE(Near(PainterTest::SolidArea(triangles), 59.0f * 19.0f + 19.0f * 40.0f - 11.0f * 11.0f, 0.5f));

    const std::vector<std::vector<Vec2>> outline{ell, hole};
    for (const PainterTest::Triangle& triangle : triangles)
    {
        if (triangle.IsSolid())
        {
            continue;
        }
        for (std::size_t k = 0; k < 3; ++k)
        {
            // Half a pixel off an edge; at a corner, moved along its miter, half a pixel off both edges' lines.
            INFO("fringe vertex (" << triangle.corners[k].x << ", " << triangle.corners[k].y
                                   << ") lies half a pixel from the outline, or from both lines at a corner");
            const float distance = DistanceToOutline(triangle.corners[k], outline);
            REQUIRE(distance >= 0.5f - 1e-3f);
            REQUIRE(distance <= 0.5f * std::numbers::sqrt2_v<float> + 1e-3f);
        }
    }
}

TEST_CASE("A self-crossing path fills its middle by the non-zero rule and leaves it empty by the even-odd rule",
          "[painter][paths]")
{
    Fenrir::Path star = Pentagram({50, 50}, 40);

    Fenrir::DrawList2D nonZeroList;
    Fenrir::Painter nonZero(nonZeroList, PainterTest::Config());
    nonZero.FillPath(star, Colour::White);
    const std::vector<PainterTest::Triangle> nonZeroTriangles = PainterTest::Triangles(nonZeroList);

    star.SetFillRule(Fenrir::FillRule::EvenOdd);
    Fenrir::DrawList2D evenOddList;
    Fenrir::Painter evenOdd(evenOddList, PainterTest::Config());
    evenOdd.FillPath(star, Colour::White);
    const std::vector<PainterTest::Triangle> evenOddTriangles = PainterTest::Triangles(evenOddList);

    INFO("both cover the star's points");
    REQUIRE(PainterTest::Covered(nonZeroTriangles, {50, 16}));
    REQUIRE(PainterTest::Covered(evenOddTriangles, {50, 16}));
    INFO("the middle, wound round twice, is covered by the non-zero rule only");
    REQUIRE(PainterTest::Covered(nonZeroTriangles, {50, 50}));
    REQUIRE_FALSE(PainterTest::Covered(evenOddTriangles, {50, 50}));
    INFO("even-odd is five separate points, a triangle each, and non-zero one ten-sided outline round them all");
    REQUIRE(PainterTest::Count(evenOddTriangles, true) == 5);
    REQUIRE(PainterTest::Count(evenOddTriangles, false) == 5 * 3 * 2);
    REQUIRE(PainterTest::Count(nonZeroTriangles, false) == 10 * 2);
}

TEST_CASE("Quadrupling the flattening tolerance halves the segments of a curve", "[painter][paths]")
{
    // The quadratic's second difference is (0, -64): split into n lines it strays 64 / (4 n^2), so 8 lines keep within
    // a quarter pixel and 4 within a pixel.
    Fenrir::Path arch;
    arch.MoveTo({0, 0}).QuadTo({50, 32}, {100, 0}).Close();
    const auto segments = [&arch](float tolerance) {
        Fenrir::DrawList2D list;
        Fenrir::PainterConfig config = PainterTest::Config();
        config.tolerance = tolerance;
        Fenrir::Painter painter(list, config);
        painter.FillPath(arch, Colour::White);
        // Two fringe triangles an edge, less the closing line.
        return PainterTest::Count(PainterTest::Triangles(list), false) / 2 - 1;
    };

    INFO("eight lines at a quarter pixel, four at a pixel");
    REQUIRE(segments(0.25f) == 8);
    REQUIRE(segments(1.0f) == 4);
}

TEST_CASE("The tolerance is in pixels under the current transform", "[painter][paths]")
{
    Fenrir::Path arch;
    arch.MoveTo({0, 0}).QuadTo({50, 32}, {100, 0}).Close();
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());
    // A quarter the size, the curve strays a quarter as far: half the lines keep within the same quarter pixel.
    painter.Scale({0.25f, 0.25f});
    painter.FillPath(arch, Colour::White);

    INFO("four lines along the curve");
    REQUIRE(PainterTest::Count(PainterTest::Triangles(list), false) / 2 - 1 == 4);
}

TEST_CASE("A transformed path fills where the path fills under that transform", "[painter][paths]")
{
    Fenrir::Path path;
    path.AddPolygon(std::vector<Vec2>{{0, 0}, {40, 0}, {20, 30}});
    const Fenrir::Transform2D move = Fenrir::Transform2D::Translation({5, 7}) * Fenrir::Transform2D::Rotation(0.5f);

    Fenrir::DrawList2D kept;
    Fenrir::Painter keptPainter(kept, PainterTest::Config());
    keptPainter.FillPath(path.Transformed(move), Colour::White);

    Fenrir::DrawList2D drawn;
    Fenrir::Painter drawnPainter(drawn, PainterTest::Config());
    drawnPainter.Concat(move);
    drawnPainter.FillPath(path, Colour::White);

    const std::vector<Fenrir::PainterVertex> a = PainterTest::Vertices(kept);
    const std::vector<Fenrir::PainterVertex> b = PainterTest::Vertices(drawn);
    INFO("the same vertices at the same positions");
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        REQUIRE(Near(a[i].position, b[i].position, 1e-3f));
    }
}

TEST_CASE("A path fill takes its paint and the clip", "[painter][paths]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());
    Fenrir::Path triangle;
    triangle.AddPolygon(std::vector<Vec2>{{0, 0}, {40, 0}, {0, 40}});
    Fenrir::LinearGradient gradient;
    gradient.start = {0, 0};
    gradient.end = {40, 0};
    gradient.stops = {{0.0f, Colour::Black}, {1.0f, Colour::White}};

    painter.ClipRoundedRect({0, 0, 100, 100}, Fenrir::CornerRadii::All(8));
    painter.FillPath(triangle, gradient);

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    INFO("the triangle's three corners, inside and outside the edge");
    REQUIRE(vertices.size() == 6);
    for (const Fenrir::PainterVertex& vertex : vertices)
    {
        INFO("each path vertex carries the gradient, its offset along it in uv.x, and the clip");
        REQUIRE(PainterTest::PaintOf(vertex) == Fenrir::PaintKind::LinearGradient);
        REQUIRE(Near(vertex.uv.x, vertex.position.x / 40.0f));
        REQUIRE(PainterTest::HasFlag(vertex, Fenrir::PainterFlags::Clipped));
        REQUIRE(Near(vertex.clipPoint, vertex.position - Vec2(50, 50)));
    }
}

TEST_CASE("A path fill batches with the shapes before it", "[painter][paths]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());
    Fenrir::Path triangle;
    triangle.AddPolygon(std::vector<Vec2>{{0, 0}, {40, 0}, {0, 40}});

    painter.DrawRect({0, 0, 10, 10}, Colour::Red);
    painter.FillPath(triangle, Colour::White);

    INFO("the rectangle and the path share one command: four vertices and the path's six, 6 + 3 * 7 indices");
    REQUIRE(list.commands.size() == 1);
    REQUIRE(PainterTest::Draw(list, 0).indexCount == 27);
    REQUIRE(PainterTest::Vertices(list).size() == 10);
    INFO("the path's indices count from the command's base, after the rectangle's four vertices");
    const std::vector<std::uint16_t> indices = PainterTest::Indices16(list);
    for (std::size_t i = 6; i < indices.size(); ++i)
    {
        REQUIRE(indices[i] >= 4);
        REQUIRE(indices[i] < 10);
    }
}

TEST_CASE("A path past 16-bit indices' 65536 vertices is cut into commands of whole triangles", "[painter][paths]")
{
    // 90 by 100 separate squares: 36000 corners, each inside and outside its edge, 72000 vertices.
    Fenrir::Path squares;
    for (int row = 0; row < 100; ++row)
    {
        for (int column = 0; column < 90; ++column)
        {
            squares.AddRect({static_cast<float>(column) * 4.0f, static_cast<float>(row) * 4.0f, 2.0f, 2.0f});
        }
    }
    const auto draw = [&squares](Fenrir::IndexType2D type) {
        Fenrir::DrawList2D list;
        Fenrir::PainterConfig config = PainterTest::Config();
        config.indexType = type;
        Fenrir::Painter painter(list, config);
        painter.FillPath(squares, Colour::White);
        return list;
    };

    const Fenrir::DrawList2D wide = draw(Fenrir::IndexType2D::UInt32);
    INFO("with 32-bit indices one command of 9000 squares' 10 triangles");
    REQUIRE(wide.commands.size() == 1);
    REQUIRE(PainterTest::Draw(wide, 0).indexCount == 9000u * 10u * 3u);

    const Fenrir::DrawList2D narrow = draw(Fenrir::IndexType2D::UInt16);
    INFO("with 16-bit indices two commands or more, each within 65536 vertices, the same triangles between them");
    REQUIRE(narrow.commands.size() >= 2);
    std::uint32_t indices = 0;
    for (std::size_t i = 0; i < narrow.commands.size(); ++i)
    {
        const Fenrir::DrawCommand2D& command = PainterTest::Draw(narrow, i);
        indices += command.indexCount;
        const std::size_t next = i + 1 < narrow.commands.size()
                                     ? static_cast<std::size_t>(PainterTest::Draw(narrow, i + 1).baseVertex)
                                     : narrow.vertices.size() / sizeof(Fenrir::PainterVertex);
        REQUIRE(next - static_cast<std::size_t>(command.baseVertex) <= 65536u);
    }
    REQUIRE(indices == 9000u * 10u * 3u);
}
