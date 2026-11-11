// The painter's paints by what they append to a 2D draw list (the spec's seam 2): gradients' stops, spread and
// transform, the stop texture for more than two stops, and images, nine-sliced or whole.

#include <cstdint>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirPainter/Painter.hpp"
#include "FenrirPainter/StopTexture.hpp"
#include "PainterTest.hpp"

using Fenrir::Colour;
using Fenrir::Math::Vec2;
using Fenrir::Math::Vec4;
using PainterTest::Near;
using PainterTest::Rgba8;

namespace
{
    Fenrir::LinearGradient RedToBlue()
    {
        Fenrir::LinearGradient gradient;
        gradient.start = {0, 0};
        gradient.end = {100, 0};
        gradient.stops = {{0.25f, Colour::Red}, {0.75f, Colour::Blue}};
        return gradient;
    }

    constexpr Fenrir::AssetHandle<Fenrir::Image> Picture{3, 1};
} // namespace

TEST_CASE("A linear gradient's two stops and its transform land in the vertex", "[painter][paints]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());
    Fenrir::LinearGradient moved = RedToBlue();
    moved.transform = Fenrir::Transform2D::Translation({50, 0});
    moved.spread = Fenrir::GradientSpread::Repeat;

    painter.DrawRect({0, 0, 100, 10}, RedToBlue());
    painter.DrawRect({0, 0, 100, 10}, moved);

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    INFO("a linear gradient from red at 0.25 to blue at 0.75, padded");
    REQUIRE(PainterTest::PaintOf(vertices[0]) == Fenrir::PaintKind::LinearGradient);
    REQUIRE(vertices[0].colour == Rgba8(255, 0, 0, 255));
    REQUIRE(vertices[0].colour2 == Rgba8(0, 0, 255, 255));
    REQUIRE(Near(Vec2(vertices[0].extents.z, vertices[0].extents.w), Vec2(0.25f, 0.75f)));
    REQUIRE_FALSE(PainterTest::HasFlag(vertices[0], Fenrir::PainterFlags::Repeat));
    // The quad runs from x = -1 to 101, so the offset along the gradient runs from -0.01 to 1.01.
    INFO("each vertex's uv is its offset along the gradient");
    REQUIRE(Near(vertices[0].uv.x, -0.01f));
    REQUIRE(Near(vertices[1].uv.x, 1.01f));
    INFO("the gradient's own transform moves it: the offset at x = -1 is (-1 - 50) / 100");
    REQUIRE(Near(vertices[4].uv.x, -0.51f));
    REQUIRE(Near(vertices[5].uv.x, 0.51f));
    REQUIRE(PainterTest::HasFlag(vertices[4], Fenrir::PainterFlags::Repeat));
}

TEST_CASE("A radial gradient's vertex is in the space of its circle, with its focal point", "[painter][paints]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());
    Fenrir::RadialGradient gradient;
    gradient.centre = {50, 50};
    gradient.radius = 50;
    gradient.focal = {60, 50};
    gradient.stops = {{0.0f, Colour::White}, {1.0f, Colour::Black}};

    painter.DrawRect({0, 0, 100, 100}, gradient);

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    INFO("a radial gradient, white to black");
    REQUIRE(PainterTest::PaintOf(vertices[0]) == Fenrir::PaintKind::RadialGradient);
    REQUIRE(vertices[0].colour == Rgba8(255, 255, 255, 255));
    REQUIRE(vertices[0].colour2 == Rgba8(0, 0, 0, 255));
    INFO("the focal point (60, 50) is (0.2, 0) of the unit circle; the stops at 0 and 1");
    REQUIRE(Near(vertices[0].extents, Vec4(0.2f, 0.0f, 0.0f, 1.0f)));
    INFO("the corner (-1, -1) is (-1.02, -1.02) of the unit circle");
    REQUIRE(Near(vertices[0].uv, Vec2(-1.02f, -1.02f)));
}

TEST_CASE("A gradient of more than two stops reads a row of the stop texture", "[painter][paints]")
{
    Fenrir::DrawList2D list;
    Fenrir::StopTexture stops(Picture, 4);
    Fenrir::PainterConfig config = PainterTest::Config();
    config.stops = &stops;
    Fenrir::Painter painter(list, config);
    Fenrir::LinearGradient three = RedToBlue();
    three.stops.push_back({1.0f, Colour::White});

    painter.DrawRect({0, 0, 100, 10}, three);
    painter.DrawRect({0, 20, 100, 10}, three);

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    INFO("the vertex reads row 0 of 4, its centre at V 0.125");
    REQUIRE(PainterTest::HasFlag(vertices[0], Fenrir::PainterFlags::StopRow));
    REQUIRE(Near(vertices[0].extents.z, 0.125f));
    INFO("equal stops share the row");
    REQUIRE(stops.RowCount() == 1);
    REQUIRE(Near(vertices[4].extents.z, 0.125f));
    INFO("the command binds the stop texture in its slot, the image slot left empty");
    REQUIRE(list.commands.size() == 1);
    REQUIRE(list.commands[0].textures.size() == 2);
    REQUIRE(list.commands[0].textures[Fenrir::PainterStopSlot].image == Picture);
    REQUIRE(list.commands[0].textures[Fenrir::PainterImageSlot].image.IsEmpty());
}

TEST_CASE("The stop texture's row holds the gradient's colours, premultiplied", "[painter][paints]")
{
    Fenrir::StopTexture stops(Picture, 2);
    const std::vector<Fenrir::GradientStop> ramp{
        {0.0f, Colour::Red}, {0.5f, Colour{0.0f, 0.0f, 1.0f, 0.0f}}, {1.0f, Colour::White}};

    REQUIRE(stops.AddRow(ramp).has_value());
    const Fenrir::ImageData image = stops.TakeImageData();

    INFO("an RGBA8 image 256 wide and 2 rows high, stored linear");
    REQUIRE(image.width == Fenrir::StopTexture::Width);
    REQUIRE(image.height == 2);
    REQUIRE(image.colourSpace == Fenrir::ColourSpace::Linear);
    REQUIRE(image.pixels.size() == 256u * 2u * 4u);
    const auto texel = [&](std::size_t x, std::size_t y) {
        const std::size_t at = (y * 256 + x) * 4;
        return Rgba8(
            std::to_integer<std::uint32_t>(image.pixels[at]), std::to_integer<std::uint32_t>(image.pixels[at + 1]),
            std::to_integer<std::uint32_t>(image.pixels[at + 2]), std::to_integer<std::uint32_t>(image.pixels[at + 3]));
    };
    INFO("the first texel is the first stop, the last the last");
    REQUIRE(texel(0, 0) == Rgba8(255, 0, 0, 255));
    REQUIRE(texel(255, 0) == Rgba8(255, 255, 255, 255));
    // Texel 128 is at offset 128 / 255 = 0.502, just past the transparent middle stop: almost transparent.
    INFO("halfway, the transparent stop premultiplies to nearly nothing");
    REQUIRE((texel(128, 0) >> 24u) <= 2u);
    INFO("the unused row is transparent, and the texture is empty for the next frame");
    REQUIRE(texel(10, 1) == 0u);
    REQUIRE(stops.RowCount() == 0);
}

TEST_CASE("Without a stop texture, or with it full, a gradient uses its first and last stops", "[painter][paints]")
{
    Fenrir::DrawList2D list;
    Fenrir::StopTexture full(Picture, 1);
    const std::vector<Fenrir::GradientStop> other{{0.0f, Colour::Black}, {0.5f, Colour::Red}, {1.0f, Colour::Black}};
    REQUIRE(full.AddRow(other).has_value());
    Fenrir::LinearGradient three = RedToBlue();
    three.stops.push_back({1.0f, Colour::White});

    Fenrir::Painter plain(list, PainterTest::Config());
    plain.DrawRect({0, 0, 100, 10}, three);
    Fenrir::PainterConfig config = PainterTest::Config();
    config.stops = &full;
    Fenrir::Painter painter(list, config);
    painter.DrawRect({0, 0, 100, 10}, three);

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    for (const std::size_t first : {0u, 4u})
    {
        INFO("vertex " << first << ": red at 0.25 to white at 1, no stop row");
        REQUIRE_FALSE(PainterTest::HasFlag(vertices[first], Fenrir::PainterFlags::StopRow));
        REQUIRE(vertices[first].colour == Rgba8(255, 0, 0, 255));
        REQUIRE(vertices[first].colour2 == Rgba8(255, 255, 255, 255));
        REQUIRE(Near(Vec2(vertices[first].extents.z, vertices[first].extents.w), Vec2(0.25f, 1.0f)));
    }
}

TEST_CASE("An image into a rectangle is one quad over the whole image, with its sampler", "[painter][paints]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());
    Fenrir::ImageDraw image;
    image.image = Picture;
    image.sampler.filter = Fenrir::SamplerFilter::Nearest;
    image.tint = Colour{1.0f, 1.0f, 1.0f, 0.5f};

    painter.DrawImage({10, 10, 20, 40}, image);

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    REQUIRE(vertices.size() == 4);
    INFO("an image paint in slot 0, tinted half transparent");
    REQUIRE(PainterTest::ShapeOf(vertices[0]) == Fenrir::ShapeKind::Rectangle);
    REQUIRE(PainterTest::PaintOf(vertices[0]) == Fenrir::PaintKind::Image);
    REQUIRE(vertices[0].kinds[3] == Fenrir::PainterImageSlot);
    REQUIRE(vertices[0].colour == Rgba8(128, 128, 128, 128));
    // The quad's corner (9, 9) is one unit outside the image's (10, 10): -1/20 across and -1/40 down.
    INFO("the uv runs 0 to 1 over the rectangle, past it by the margin");
    REQUIRE(Near(vertices[0].uv, Vec2(-0.05f, -0.025f)));
    REQUIRE(Near(vertices[2].uv, Vec2(1.05f, 1.025f)));
    INFO("the command binds the image with the sampler");
    REQUIRE(list.commands.at(0).textures.size() == 1);
    REQUIRE(list.commands[0].textures[0] == Fenrir::TextureSlot2D{Picture, image.sampler});
}

TEST_CASE("A nine-slice image is nine quads whose corners keep their size", "[painter][paints]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());
    Fenrir::ImageDraw image;
    image.image = Picture;
    image.nineSlice = Fenrir::NineSlice{{8, 4, 8, 4}, {32, 16}};

    painter.DrawImage({0, 0, 100, 50}, image);

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    INFO("nine quads in one command");
    REQUIRE(vertices.size() == 36);
    REQUIRE(list.commands.size() == 1);
    REQUIRE(list.commands[0].indexCount == 54);
    // Row by row, left to right: the top-left corner is (0, 0) to (4, 8) in the rectangle and (0, 0) to (4/32, 8/16) of
    // the image (left 4, top 8). Only the image's outer edges are anti-aliased: a piece's quad ends at its seams, where
    // its shape runs on one unit so the seam is fully covered from both sides.
    INFO("the top-left corner: 4 by 8 units over the image's top-left 4 by 8 pixels");
    REQUIRE(Near(vertices[0].position, Vec2(-1, -1)));
    REQUIRE(Near(vertices[2].position, Vec2(4, 8)));
    REQUIRE(Near(vertices[0].halfSize, Vec2(2.5f, 4.5f)));
    REQUIRE(Near(vertices[0].uv, Vec2(-1.0f / 32.0f, -1.0f / 16.0f)));
    REQUIRE(Near(vertices[2].uv, Vec2(4.0f / 32.0f, 8.0f / 16.0f)));
    INFO("the middle stretches 92 by 34 units over the image's middle 24 by 0 pixels, its quad ending at its seams");
    REQUIRE(Near(vertices[16].position, Vec2(4, 8)));
    REQUIRE(Near(vertices[18].position, Vec2(96, 42)));
    REQUIRE(Near(vertices[16].uv, Vec2(4.0f / 32.0f, 8.0f / 16.0f)));
    REQUIRE(Near(vertices[18].uv, Vec2(28.0f / 32.0f, 8.0f / 16.0f)));
    REQUIRE(Near(vertices[16].halfSize, Vec2(47, 18)));
    INFO("the bottom-right corner keeps its 4 by 8 units");
    REQUIRE(Near(vertices[32].position, Vec2(96, 42)));
    REQUIRE(Near(vertices[34].position, Vec2(101, 51)));
}

TEST_CASE("An image pattern paint maps its rectangle to the image", "[painter][paints]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());
    Fenrir::ImagePattern pattern;
    pattern.image = Picture;
    pattern.rect = {0, 0, 10, 10};
    pattern.sampler.wrapU = Fenrir::SamplerWrap::Repeat;

    painter.DrawCircle({20, 20}, 9, pattern);

    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    INFO("a circle painted with the image, its quad's corner (10, 10) at uv (1, 1)");
    REQUIRE(PainterTest::PaintOf(vertices[0]) == Fenrir::PaintKind::Image);
    REQUIRE(Near(vertices[0].uv, Vec2(1, 1)));
    REQUIRE(list.commands.at(0).textures.at(0).sampler.wrapU == Fenrir::SamplerWrap::Repeat);
}
