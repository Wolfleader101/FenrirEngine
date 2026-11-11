// The painter's clip stack and batching by what they append to a 2D draw list (the spec's seam 2): the clip stack's
// bounds become the command's scissor and its innermost clip the vertex's; same-state draws share a command; 16-bit
// indices roll over into a new command with a base vertex.

#include <cstdint>
#include <numbers>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirPainter/Painter.hpp"
#include "PainterTest.hpp"

using Fenrir::Colour;
using Fenrir::Rect2D;
using Fenrir::Math::Vec2;
using Fenrir::Math::Vec4;
using PainterTest::Near;

namespace
{
    bool Near(const Rect2D& a, const Rect2D& b)
    {
        return PainterTest::Near(a.x, b.x) && PainterTest::Near(a.y, b.y) && PainterTest::Near(a.width, b.width) &&
               PainterTest::Near(a.height, b.height);
    }

    constexpr Fenrir::AssetHandle<Fenrir::Image> First{3, 1};
    constexpr Fenrir::AssetHandle<Fenrir::Image> Second{4, 1};

    Fenrir::ImageDraw Image(Fenrir::AssetHandle<Fenrir::Image> image)
    {
        Fenrir::ImageDraw draw;
        draw.image = image;
        return draw;
    }
} // namespace

TEST_CASE("A rectangle clip is the command's scissor and the vertex's clip", "[painter][clips]")
{
    Fenrir::DrawList2D list;
    Fenrir::PainterConfig config = PainterTest::Config();
    config.scale = 2.0f;
    Fenrir::Painter painter(list, config);

    painter.ClipRect({10, 10, 50, 20});
    painter.DrawRect({0, 0, 100, 100}, Colour::White);

    REQUIRE(list.commands.size() == 1);
    INFO("the scissor is the clip in logical units, at root scale 2");
    REQUIRE(list.commands[0].scissor.has_value());
    REQUIRE(Near(*list.commands[0].scissor, Rect2D{20, 20, 100, 40}));
    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    // The clip's centre is (70, 40) in logical units; the quad's corner (-0.5, -0.5) in UI units is (-1, -1).
    INFO("the vertex carries the clip, centred on its own centre, in logical units");
    REQUIRE(PainterTest::HasFlag(vertices[0], Fenrir::PainterFlags::Clipped));
    REQUIRE(Near(vertices[0].clipHalfSize, Vec2(50, 20)));
    REQUIRE(Near(vertices[0].clipPoint, Vec2(-71, -41)));
    REQUIRE(Near(vertices[0].clipRadii, Vec4(0)));
}

TEST_CASE("Nested clips intersect their bounds in the scissor and keep the innermost per pixel", "[painter][clips]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());

    painter.Save();
    painter.ClipRect({0, 0, 100, 100});
    painter.Save();
    painter.ClipRect({50, 20, 100, 30});
    painter.DrawRect({0, 0, 10, 10}, Colour::White);
    painter.Save();
    // A quarter turn about the origin: the rounded clip (10, 10, 20, 40) lands on (-50, 10) to (-10, 30).
    painter.Rotate(std::numbers::pi_v<float> / 2.0f);
    painter.ClipRoundedRect({10, 10, 20, 40}, Fenrir::CornerRadii::All(3));
    painter.DrawRect({0, 0, 10, 10}, Colour::White);
    painter.Restore();
    painter.Restore();
    painter.DrawRect({0, 0, 10, 10}, Colour::White);
    painter.Restore();
    painter.DrawRect({0, 0, 10, 10}, Colour::White);

    INFO("three draws appended, in three commands: the rotated clip's draw appended nothing");
    REQUIRE(list.commands.size() == 3);
    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    REQUIRE(vertices.size() == 12);
    INFO("two axis-aligned rectangle clips are their exact intersection, in the scissor and in the vertex");
    REQUIRE(Near(*list.commands[0].scissor, Rect2D{50, 20, 50, 30}));
    REQUIRE(Near(vertices[0].clipHalfSize, Vec2(25, 15)));
    INFO("Restore brings back the outer clip, and then none");
    REQUIRE(Near(*list.commands[1].scissor, Rect2D{0, 0, 100, 100}));
    REQUIRE(Near(vertices[4].clipHalfSize, Vec2(50, 50)));
    REQUIRE(list.commands[2].scissor == std::nullopt);
    REQUIRE_FALSE(PainterTest::HasFlag(vertices[8], Fenrir::PainterFlags::Clipped));
}

TEST_CASE("A rotated rounded clip is the vertex's clip, with its radii, under its transform", "[painter][clips]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());

    painter.ClipRect({-100, 0, 200, 100});
    painter.Translate({0, 50});
    painter.Rotate(std::numbers::pi_v<float> / 2.0f);
    // Rotated a quarter turn about (0, 50): the clip (-10, -20) to (10, 20) lands on (-20, 40) to (20, 60).
    painter.ClipRoundedRect({-10, -20, 20, 40}, Fenrir::CornerRadii::All(4));
    painter.DrawCircle({0, 0}, 5, Colour::White);

    REQUIRE(list.commands.size() == 1);
    INFO("the scissor is the outer clip met by the rotated clip's bounds");
    REQUIRE(Near(*list.commands[0].scissor, Rect2D{-20, 40, 40, 20}));
    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    INFO("the vertex's clip is the rounded one, its half size and radii in its own unrotated space");
    REQUIRE(Near(vertices[0].clipHalfSize, Vec2(10, 20)));
    REQUIRE(Near(vertices[0].clipRadii, Vec4(4, 4, 4, 4)));
    // The circle's quad corner (-6, -6) in its space is, after the rotation, (6, -6) about (0, 50): (6, 44) in logical
    // units, which is (-6, -6) of the clip's space again.
    REQUIRE(Near(vertices[0].clipPoint, Vec2(-6, -6), 1e-3f));
}

TEST_CASE("Same-state draws share one command; a texture or scissor change starts another", "[painter][batching]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());

    painter.DrawRect({0, 0, 10, 10}, Colour::White);
    painter.DrawRoundedRect({0, 0, 10, 10}, Fenrir::CornerRadii::All(2), Colour::Red);
    // The first image fills the image slot no draw of the command used yet.
    painter.DrawImage({0, 0, 10, 10}, Image(First));
    painter.DrawCircle({5, 5}, 5, Colour::Blue);
    painter.DrawImage({0, 0, 10, 10}, Image(First));
    painter.DrawImage({0, 0, 10, 10}, Image(Second));
    painter.ClipRect({0, 0, 5, 5});
    painter.DrawImage({0, 0, 10, 10}, Image(Second));
    painter.DrawRect({0, 0, 10, 10}, Colour::White);

    INFO("five draws in one command, then the second image, then the clip");
    REQUIRE(list.commands.size() == 3);
    REQUIRE(list.commands[0].indexCount == 30);
    REQUIRE(list.commands[0].textures.at(0).image == First);
    REQUIRE(list.commands[1].indexCount == 6);
    REQUIRE(list.commands[1].textures.at(0).image == Second);
    REQUIRE(list.commands[2].indexCount == 12);
    REQUIRE(list.commands[2].scissor.has_value());
    INFO("one pipeline for them all");
    REQUIRE(list.pipelines.size() == 1);
}

TEST_CASE("Sixteen-bit indices roll over into a new command with a base vertex past 65535 vertices",
          "[painter][batching]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());

    // 16384 quads are 65536 vertices, indices 0 to 65535: the most one command of 16-bit indices reaches.
    for (int i = 0; i < 16385; ++i)
    {
        painter.DrawRect({0, 0, 1, 1}, Colour::White);
    }

    INFO("16384 quads in the first command, the 16385th in a second from vertex 65536");
    REQUIRE(list.commands.size() == 2);
    REQUIRE(list.commands[0].indexCount == 16384u * 6u);
    REQUIRE(list.commands[0].baseVertex == 0);
    REQUIRE(list.commands[1].indexCount == 6);
    REQUIRE(list.commands[1].baseVertex == 65536);
    REQUIRE(list.commands[1].indexOffset == 16384u * 6u);
    const std::vector<std::uint16_t> indices = PainterTest::Indices16(list);
    INFO("the last command's indices count from its base vertex");
    REQUIRE(indices[16384u * 6u] == 0);
    REQUIRE(indices[16384u * 6u - 1u] == 65535);
}

TEST_CASE("With 32-bit indices one command takes any number of vertices", "[painter][batching]")
{
    Fenrir::DrawList2D list;
    Fenrir::PainterConfig config = PainterTest::Config();
    config.indexType = Fenrir::IndexType2D::UInt32;
    Fenrir::Painter painter(list, config);

    for (int i = 0; i < 16385; ++i)
    {
        painter.DrawRect({0, 0, 1, 1}, Colour::White);
    }

    INFO("one command, its pipeline on 32-bit indices");
    REQUIRE(list.commands.size() == 1);
    REQUIRE(list.commands[0].indexCount == 16385u * 6u);
    REQUIRE(list.pipelines.at(0).indexType == Fenrir::IndexType2D::UInt32);
    REQUIRE(list.indices.size() == 16385u * 6u * 4u);
}
