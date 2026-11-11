// The painter's analytic shapes by what they append to a 2D draw list (the spec's seam 2): one quad of four fat
// vertices a shape, carrying the shape's kind and numbers. Expected values are worked by hand from the draws'
// arguments.

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirPainter/Painter.hpp"
#include "PainterTest.hpp"

using Fenrir::Colour;
using Fenrir::Math::Vec2;
using Fenrir::Math::Vec4;
using PainterTest::Near;
using PainterTest::Rgba8;

TEST_CASE("A rectangle is one quad of four vertices and two triangles, its shape in each vertex", "[painter][shapes]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());

    painter.DrawRect({10.0f, 20.0f, 100.0f, 50.0f}, Colour::Red);

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    INFO("one command of six indices over four vertices, drawn with the painter's pipeline");
    REQUIRE(list.commands.size() == 1);
    REQUIRE(PainterTest::Draw(list, 0).indexCount == 6);
    REQUIRE(vertices.size() == 4);
    REQUIRE(list.pipelines.size() == 1);
    REQUIRE(list.pipelines[0] == Fenrir::PainterPipeline(PainterTest::Shader, Fenrir::BlendSpace2D::Linear));
    REQUIRE(PainterTest::Indices16(list) == std::vector<std::uint16_t>{0, 1, 2, 0, 2, 3});

    // The rectangle's centre is (60, 45) and its half size (50, 25); the quad is one unit wider on every side for
    // the anti-aliased edge.
    const std::array<Vec2, 4> locals{Vec2(-51, -26), Vec2(51, -26), Vec2(51, 26), Vec2(-51, 26)};
    for (std::size_t i = 0; i < 4; ++i)
    {
        INFO("vertex " << i << " is a corner of the quad, in logical units and in the shape's space");
        REQUIRE(Near(vertices[i].position, Vec2(60, 45) + locals[i]));
        REQUIRE(Near(vertices[i].local, locals[i]));
        INFO("and carries the shape: a rectangle of half size (50, 25), solid opaque red, unclipped");
        REQUIRE(PainterTest::ShapeOf(vertices[i]) == Fenrir::ShapeKind::Rectangle);
        REQUIRE(PainterTest::PaintOf(vertices[i]) == Fenrir::PaintKind::Solid);
        REQUIRE(Near(vertices[i].halfSize, Vec2(50, 25)));
        REQUIRE(vertices[i].colour == Rgba8(255, 0, 0, 255));
        REQUIRE_FALSE(PainterTest::HasFlag(vertices[i], Fenrir::PainterFlags::Clipped));
    }
}

TEST_CASE("Colours are encoded to sRGB and premultiplied in the vertex", "[painter][shapes]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());

    // Linear 0.2158605 is sRGB 128; white at half alpha premultiplies to 128 in every channel.
    painter.DrawRect({0, 0, 10, 10}, Colour::Grey);
    painter.DrawRect({0, 0, 10, 10}, Colour{1.0f, 1.0f, 1.0f, 0.5f});
    painter.DrawRect({0, 0, 10, 10}, Colour{1.0f, 0.0f, 0.0f, 0.0f});

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    INFO("grey is sRGB 128, opaque");
    REQUIRE(vertices[0].colour == Rgba8(128, 128, 128, 255));
    INFO("half-transparent white is 128 in colour and alpha");
    REQUIRE(vertices[4].colour == Rgba8(128, 128, 128, 128));
    INFO("a transparent colour is all zeros");
    REQUIRE(vertices[8].colour == 0u);
}

TEST_CASE("A rounded rectangle carries its corner radii", "[painter][shapes]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());

    painter.DrawRoundedRect({0, 0, 100, 50}, {1, 2, 3, 4}, Colour::White);

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    INFO("a rounded rectangle with radii 1, 2, 3, 4: top-left, top-right, bottom-right, bottom-left");
    REQUIRE(PainterTest::ShapeOf(vertices[0]) == Fenrir::ShapeKind::RoundedRectangle);
    REQUIRE(Near(vertices[0].radii, Vec4(1, 2, 3, 4)));
}

TEST_CASE("Corner radii too large for their side are scaled down together", "[painter][shapes]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());

    // 40 + 40 down each 50-unit side is too much: every radius is scaled by 50 / 80.
    painter.DrawRoundedRect({0, 0, 100, 50}, Fenrir::CornerRadii::All(40), Colour::White);

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    INFO("radius 40 on every corner of a 100 by 50 rectangle becomes 25");
    REQUIRE(Near(vertices[0].radii, Vec4(25, 25, 25, 25)));
}

TEST_CASE("A lone corner radius reaches at most half the rectangle's shorter side", "[painter][shapes]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());

    // 20 fits beside the bottom-left's 0 down the 20-unit side, but a circular corner of 20 would overrun the box.
    painter.DrawRoundedRect({0, 0, 100, 20}, {20, 0, 0, 0}, Colour::White);

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    INFO("the top-left radius is capped at 10, the others stay sharp");
    REQUIRE(Near(vertices[0].radii, Vec4(10, 0, 0, 0)));
}

TEST_CASE("A border carries its widths and colours per side", "[painter][shapes]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());
    Fenrir::Border border;
    border.widths = {1, 2, 3, 4};
    border.top = Colour::Red;
    border.right = Colour::Green;
    border.bottom = Colour::Blue;
    border.left = Colour::White;

    painter.DrawBorder({0, 0, 40, 30}, Fenrir::CornerRadii::All(5), border);

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    REQUIRE(vertices.size() == 4);
    INFO("a border of half size (20, 15) and radius 5, its widths top, right, bottom, left");
    REQUIRE(PainterTest::ShapeOf(vertices[0]) == Fenrir::ShapeKind::Border);
    REQUIRE(Near(vertices[0].halfSize, Vec2(20, 15)));
    REQUIRE(Near(vertices[0].radii, Vec4(5, 5, 5, 5)));
    REQUIRE(Near(vertices[0].shape, Vec4(1, 2, 3, 4)));
    INFO("and its colours in the same order");
    REQUIRE(vertices[0].borderColours == std::array<std::uint32_t, 4>{Rgba8(255, 0, 0, 255), Rgba8(0, 255, 0, 255),
                                                                      Rgba8(0, 0, 255, 255),
                                                                      Rgba8(255, 255, 255, 255)});
}

TEST_CASE("An outset shadow's quad reaches past its offset, spread and blur", "[painter][shapes]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());
    Fenrir::Shadow outset;
    outset.offset = {5, 6};
    outset.blur = 10;
    outset.spread = 2;

    painter.DrawShadow({0, 0, 100, 50}, Fenrir::CornerRadii::All(4), outset);

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    REQUIRE(vertices.size() == 4);
    INFO("a shadow of the box's half size, its blur, spread and offset in the shape numbers, not inset");
    REQUIRE(PainterTest::ShapeOf(vertices[0]) == Fenrir::ShapeKind::Shadow);
    REQUIRE(Near(vertices[0].halfSize, Vec2(50, 25)));
    REQUIRE(Near(vertices[0].shape, Vec4(10, 2, 5, 6)));
    REQUIRE(vertices[0].colour == Rgba8(0, 0, 0, 255));
    REQUIRE_FALSE(PainterTest::HasFlag(vertices[0], Fenrir::PainterFlags::Inset));
    // Blur 10 is a standard deviation of 5, which the quad covers three times over: 50 + 5 + 2 + 15 + 1 to the right.
    INFO("the quad reaches past the offset, spread and three standard deviations of blur, plus the margin");
    REQUIRE(Near(vertices[2].local, Vec2(73, 49)));
    REQUIRE(Near(vertices[0].local, Vec2(-63, -37)));
}

TEST_CASE("An inset shadow is flagged and its quad covers only its box", "[painter][shapes]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());
    Fenrir::Shadow inset;
    inset.offset = {5, 6};
    inset.blur = 10;
    inset.spread = 2;
    inset.inset = true;

    painter.DrawShadow({0, 0, 100, 50}, Fenrir::CornerRadii::All(4), inset);

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    INFO("the inset flag is set, and the quad is the 100 by 50 box with its one-unit margin");
    REQUIRE(PainterTest::HasFlag(vertices[0], Fenrir::PainterFlags::Inset));
    REQUIRE(Near(vertices[0].local, Vec2(-51, -26)));
    REQUIRE(Near(vertices[2].local, Vec2(51, 26)));
}

TEST_CASE("A circle is centred on its centre with its radius as half size", "[painter][shapes]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());

    painter.DrawCircle({50, 40}, 10, Colour::White);

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    INFO("a circle of half size (10, 10), its quad's corner one unit past (40, 30)");
    REQUIRE(PainterTest::ShapeOf(vertices[0]) == Fenrir::ShapeKind::Circle);
    REQUIRE(Near(vertices[0].halfSize, Vec2(10, 10)));
    REQUIRE(Near(vertices[0].position, Vec2(39, 29)));
}

TEST_CASE("An arc carries its radius, width, middle angle and half its sweep", "[painter][shapes]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());
    constexpr float Pi = std::numbers::pi_v<float>;

    painter.DrawArc({50, 40}, 20, 0.0f, Pi / 2.0f, 4, Colour::White);

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    INFO("a quarter-turn arc from angle 0: middle and half sweep a pi/4 each; its quad covers the outer edge, 22");
    REQUIRE(PainterTest::ShapeOf(vertices[0]) == Fenrir::ShapeKind::Arc);
    REQUIRE(Near(vertices[0].shape, Vec4(20, 4, Pi / 4.0f, Pi / 4.0f)));
    REQUIRE(Near(vertices[0].halfSize, Vec2(22, 22)));
}

TEST_CASE("A ring carries its outer and inner radii", "[painter][shapes]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());

    painter.DrawRing({50, 40}, 30, 25, Colour::White);

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    INFO("a ring of radii 30 and 25, its quad over the outer circle");
    REQUIRE(PainterTest::ShapeOf(vertices[0]) == Fenrir::ShapeKind::Ring);
    REQUIRE(Near(vertices[0].shape, Vec4(30, 25, 0, 0)));
    REQUIRE(Near(vertices[0].halfSize, Vec2(30, 30)));
}

TEST_CASE("An arc of a whole turn is a ring", "[painter][shapes]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());

    painter.DrawArc({0, 0}, 20, 1.0f, 7.0f, 4, Colour::White);

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    INFO("the band between radius 22 and 18");
    REQUIRE(PainterTest::ShapeOf(vertices[0]) == Fenrir::ShapeKind::Ring);
    REQUIRE(Near(vertices[0].shape, Vec4(22, 18, 0, 0)));
}

TEST_CASE("Shapes are drawn under the transform and the root scale", "[painter][shapes]")
{
    Fenrir::DrawList2D list;
    Fenrir::PainterConfig config = PainterTest::Config();
    config.scale = 2.0f;
    Fenrir::Painter painter(list, config);

    painter.Translate({10, 0});
    painter.Save();
    painter.Rotate(std::numbers::pi_v<float> / 2.0f);
    painter.DrawRect({0, 0, 4, 2}, Colour::White);
    painter.Restore();
    painter.DrawRect({0, 0, 4, 2}, Colour::White);

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    // Root scale 2: one logical unit of anti-aliasing margin is half a UI unit of the shape's space.
    INFO("the shape's space is in UI units, its margin half a unit at root scale 2");
    REQUIRE(Near(vertices[0].local, Vec2(-2.5f, -1.5f)));
    REQUIRE(Near(vertices[0].halfSize, Vec2(2, 1)));
    // Rotated a quarter turn clockwise about (10, 0): the shape's (-0.5, -0.5) corner goes to (10.5, -0.5) in UI
    // units, (21, -1) in logical units.
    INFO("rotated a quarter turn after the translation, then scaled by the root scale");
    REQUIRE(Near(vertices[0].position, Vec2(21, -1), 1e-3f));
    INFO("Restore brings back the transform without the rotation");
    REQUIRE(Near(vertices[4].position, Vec2(19, -1), 1e-3f));
}

TEST_CASE("Pixel snapping rounds a rectangle's edges to whole pixels", "[painter][shapes]")
{
    Fenrir::DrawList2D list;
    Fenrir::PainterConfig config = PainterTest::Config();
    config.snapToPixels = true;
    config.pixelsPerUnit = 2.0f;
    Fenrir::Painter painter(list, config);

    // At 2 pixels a unit, edges round to half units: 10.3 to 10.5, 60.7 to 60.5, 20.6 to 20.5, 30.7 to 30.5.
    painter.DrawRect({10.3f, 20.6f, 50.4f, 10.1f}, Colour::White);

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    INFO("the snapped rectangle is (10.5, 20.5) to (60.5, 30.5)");
    REQUIRE(Near(vertices[0].halfSize, Vec2(25, 5)));
    REQUIRE(Near(vertices[0].position, Vec2(9.5f, 19.5f)));
}

TEST_CASE("The painter appends after what a list holds, aligned to its own vertex size", "[painter][shapes]")
{
    Fenrir::DrawList2D list;
    // A builtin:2d quad first: 80 vertex bytes and six 16-bit indices, not a multiple of the painter's 140.
    const std::array<Fenrir::Vertex2D, 4> quad{};
    list.AppendVertices(std::span<const Fenrir::Vertex2D>(quad));
    const std::array<std::uint16_t, 6> indices{0, 1, 2, 0, 2, 3};
    list.AppendIndices(std::span<const std::uint16_t>(indices));
    Fenrir::PipelineDescription2D other;
    other.vertexLayout = Fenrir::Vertex2DLayout();
    list.commands.push_back(Fenrir::DrawCommand2D{.pipeline = list.AddPipeline(other), .indexCount = 6});

    Fenrir::Painter painter(list, PainterTest::Config());
    painter.DrawRect({0, 0, 10, 10}, Colour::White);

    INFO("the painter's command reads its vertices from vertex 1 of its stride, after the padded 80 bytes");
    REQUIRE(list.commands.size() == 2);
    REQUIRE(PainterTest::Draw(list, 1).baseVertex == 1);
    REQUIRE(PainterTest::Draw(list, 1).indexOffset == 6);
    REQUIRE(PainterTest::Draw(list, 1).pipeline == 1);
    REQUIRE(list.vertices.size() == 5 * sizeof(Fenrir::PainterVertex));
    REQUIRE(PainterTest::Vertices(list, 1).at(0).halfSize == Vec2(5, 5));
}
