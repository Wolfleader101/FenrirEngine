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
    REQUIRE(PainterTest::Draw(list, 0).scissor.has_value());
    REQUIRE(Near(*PainterTest::Draw(list, 0).scissor, Rect2D{20, 20, 100, 40}));
    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    // The clip's centre is (70, 40) in logical units; the quad's corner (-0.5, -0.5) in UI units is (-1, -1).
    INFO("the vertex carries the clip, centred on its own centre, in logical units");
    REQUIRE(PainterTest::HasFlag(vertices[0], Fenrir::PainterFlags::Clipped));
    REQUIRE(Near(vertices[0].clipHalfSize, Vec2(50, 20)));
    REQUIRE(Near(vertices[0].clipPoint, Vec2(-71, -41)));
    REQUIRE(Near(vertices[0].clipRadii, Vec4(0)));
}

TEST_CASE("Two axis-aligned rectangle clips meet exactly, in the scissor and in the vertex", "[painter][clips]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());

    painter.ClipRect({0, 0, 100, 100});
    painter.ClipRect({50, 20, 100, 30});
    painter.DrawRect({0, 0, 10, 10}, Colour::White);

    INFO("the scissor and the vertex's clip are both (50, 20) to (100, 50)");
    REQUIRE(Near(*PainterTest::Draw(list, 0).scissor, Rect2D{50, 20, 50, 30}));
    REQUIRE(Near(PainterTest::Vertices(list)[0].clipHalfSize, Vec2(25, 15)));
}

TEST_CASE("A draw whose clips' bounds do not meet appends nothing", "[painter][clips]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());

    painter.ClipRect({50, 20, 50, 30});
    // A quarter turn about the origin: the rounded clip (10, 10, 20, 40) lands on (-50, 10) to (-10, 30).
    painter.Rotate(std::numbers::pi_v<float> / 2.0f);
    painter.ClipRoundedRect({10, 10, 20, 40}, Fenrir::CornerRadii::All(3));
    painter.DrawRect({0, 0, 10, 10}, Colour::White);

    INFO("the scissor is empty, so the list has no command and no vertex");
    REQUIRE(list.commands.empty());
    REQUIRE(list.vertices.empty());
}

TEST_CASE("Restore brings back the clip that was saved, and then none", "[painter][clips]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());

    painter.Save();
    painter.ClipRect({0, 0, 100, 100});
    painter.Save();
    painter.ClipRect({50, 20, 100, 30});
    painter.Restore();
    painter.DrawRect({0, 0, 10, 10}, Colour::White);
    painter.Restore();
    painter.DrawRect({0, 0, 10, 10}, Colour::White);

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    REQUIRE(list.commands.size() == 2);
    INFO("the first draw is clipped by the outer clip alone");
    REQUIRE(Near(*PainterTest::Draw(list, 0).scissor, Rect2D{0, 0, 100, 100}));
    REQUIRE(Near(vertices[0].clipHalfSize, Vec2(50, 50)));
    INFO("the second by nothing");
    REQUIRE(PainterTest::Draw(list, 1).scissor == std::nullopt);
    REQUIRE_FALSE(PainterTest::HasFlag(vertices[4], Fenrir::PainterFlags::Clipped));
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
    REQUIRE(Near(*PainterTest::Draw(list, 0).scissor, Rect2D{-20, 40, 40, 20}));
    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    INFO("the vertex's clip is the rounded one, its half size and radii in its own unrotated space");
    REQUIRE(Near(vertices[0].clipHalfSize, Vec2(10, 20)));
    REQUIRE(Near(vertices[0].clipRadii, Vec4(4, 4, 4, 4)));
    // The circle's quad corner (-6, -6) in its space is, after the rotation, (6, -6) about (0, 50): (6, 44) in logical
    // units, which is (-6, -6) of the clip's space again.
    REQUIRE(Near(vertices[0].clipPoint, Vec2(-6, -6), 1e-3f));
}

TEST_CASE("Draws with the same state share one command and one pipeline", "[painter][batching]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());

    painter.DrawRect({0, 0, 10, 10}, Colour::White);
    painter.DrawRoundedRect({0, 0, 10, 10}, Fenrir::CornerRadii::All(2), Colour::Red);
    // The image fills the image slot no draw of the command used yet; the circle after it samples nothing.
    painter.DrawImage({0, 0, 10, 10}, Image(First));
    painter.DrawCircle({5, 5}, 5, Colour::Blue);
    painter.DrawImage({0, 0, 10, 10}, Image(First));

    INFO("five quads, 30 indices, in one command binding the image, with the painter's one pipeline");
    REQUIRE(list.commands.size() == 1);
    REQUIRE(PainterTest::Draw(list, 0).indexCount == 30);
    REQUIRE(PainterTest::Draw(list, 0).textures.at(0).image == First);
    REQUIRE(list.pipelines.size() == 1);
}

TEST_CASE("A different image in a filled slot starts another command", "[painter][batching]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());

    painter.DrawImage({0, 0, 10, 10}, Image(First));
    painter.DrawImage({0, 0, 10, 10}, Image(Second));

    INFO("one command for each image, each binding its own");
    REQUIRE(list.commands.size() == 2);
    REQUIRE(PainterTest::Draw(list, 0).textures.at(0).image == First);
    REQUIRE(PainterTest::Draw(list, 1).textures.at(0).image == Second);
    REQUIRE(PainterTest::Draw(list, 1).indexCount == 6);
}

TEST_CASE("A scissor change starts another command", "[painter][batching]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());

    painter.DrawRect({0, 0, 10, 10}, Colour::White);
    painter.ClipRect({0, 0, 5, 5});
    painter.DrawRect({0, 0, 10, 10}, Colour::White);

    INFO("the unclipped draw and the clipped one in commands of their own, the second with the scissor");
    REQUIRE(list.commands.size() == 2);
    REQUIRE(PainterTest::Draw(list, 0).scissor == std::nullopt);
    REQUIRE(PainterTest::Draw(list, 1).scissor.has_value());
}

TEST_CASE("A draw after an offscreen group starts its own command", "[painter][batching]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());

    painter.DrawRect({0, 0, 10, 10}, Colour::White);
    list.BeginGroup({0, 0, 10, 10});
    list.EndGroup();
    painter.DrawRect({0, 0, 10, 10}, Colour::White);

    INFO("the draw, the group's begin and end, and a new draw: the first draw is not extended past the group");
    REQUIRE(list.commands.size() == 4);
    REQUIRE(PainterTest::Draw(list, 0).indexCount == 6);
    REQUIRE(PainterTest::Draw(list, 3).indexCount == 6);
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
    REQUIRE(PainterTest::Draw(list, 0).indexCount == 16384u * 6u);
    REQUIRE(PainterTest::Draw(list, 0).baseVertex == 0);
    REQUIRE(PainterTest::Draw(list, 1).indexCount == 6);
    REQUIRE(PainterTest::Draw(list, 1).baseVertex == 65536);
    REQUIRE(PainterTest::Draw(list, 1).indexOffset == 16384u * 6u);
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
    REQUIRE(PainterTest::Draw(list, 0).indexCount == 16385u * 6u);
    REQUIRE(list.pipelines.at(0).indexType == Fenrir::IndexType2D::UInt32);
    REQUIRE(list.indices.size() == 16385u * 6u * 4u);
}
