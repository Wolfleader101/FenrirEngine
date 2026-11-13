// builtin:painter on OpenGL (the spec's GL check): a rounded rectangle, a radial gradient, a bordered box, a stroked
// star and a filled ring with a hole painted into an image 2D target (ADR-0060) through the renderer's ImageTargets2D,
// and read back at their centres, joins and edges; and offscreen groups: a half-opacity group, a blurred disc's edge,
// a masked gradient and a backdrop blur; and a word of text at two sizes, and a glyph scaled by 4 through the SDF and
// the bitmap paths. The image is linear RGBA8, cleared
// transparent each frame it is drawn, and the root's blend space Linear, so a pixel is the shader's premultiplied
// linear output to 8 bits; the expected values are worked by hand.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirPainter/Painter.hpp"
#include "FenrirPainter/Painting.hpp"
#include "FenrirPainter/Path.hpp"
#include "FenrirRenderer/ImageTargets2D.hpp"
#include "FenrirText/FontLibrary.hpp"
#include "FenrirText/Paragraph.hpp"
#include "GlScene.hpp"

using Fenrir::Colour;
using Fenrir::GlProbe;

namespace
{
    constexpr std::uint32_t Size = 64;
    /** @brief Beside the hand-worked colours: 8-bit storage and the shader's anti-aliasing. */
    constexpr float Tolerance = 0.01f;
    constexpr GlProbe::Pixel Transparent{0.0f, 0.0f, 0.0f, 0.0f};

    /** @brief An sRGB-encoded channel decoded to linear. */
    float Decode(float srgb)
    {
        return srgb <= 0.04045f ? srgb / 12.92f : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
    }

    /**
     * @brief The renderer on OpenGL with builtin:painter registered, painting into a Size-square linear image 2D
     * target and reading it back.
     */
    class GlPainter
    {
      public:
        GlPainter()
        {
            scene.RequireOpenGL();
            Fenrir::ImageData data;
            data.width = Size;
            data.height = Size;
            data.colourSpace = Fenrir::ColourSpace::Linear;
            image = scene.assets.Store().Add<Fenrir::Image>(std::move(data), Fenrir::AssetOwner::App());
        }

        /** @brief A painter config on builtin:painter, Linear. */
        [[nodiscard]] Fenrir::PainterConfig Config() const
        {
            Fenrir::PainterConfig config;
            config.shader = painting.GetShader();
            return config;
        }

        /** @brief Draws `list` into the image in one frame and reads the image back. */
        GlProbe::Image Draw(Fenrir::DrawList2D list)
        {
            scene.app.GetResource<Fenrir::ImageTargets2D>().Submit(image, std::move(list));
            scene.RunFrame();
            const Fenrir::Graphics::TextureHandle texture = Fenrir::RendererProbe::ImageTexture(scene.renderer, image);
            INFO("the renderer made the image's texture");
            REQUIRE_FALSE(texture.IsEmpty());
            return GlProbe::Read(Fenrir::RendererProbe::Device(scene.renderer), texture);
        }

        GlTest::GlScene scene;
        Fenrir::Painting painting{Fenrir::Painting::Install(scene.app)};
        Fenrir::AssetHandle<Fenrir::Image> image;
    };
} // namespace

TEST_CASE("A rounded rectangle is solid inside, half covered on an edge through pixel centres, and cut at its corners",
          "[painter][gl]")
{
    GlPainter test;
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, test.Config());
    // Its left edge runs through the centres of column 8, so they are half covered.
    painter.DrawRoundedRect({8.5f, 8.5f, 47.0f, 47.0f}, Fenrir::CornerRadii::All(12), Colour::Red);

    const GlProbe::Image image = test.Draw(std::move(list));

    INFO("opaque red at the centre and just inside the top edge");
    REQUIRE(GlProbe::Near(image.At(32, 32), {1.0f, 0.0f, 0.0f, 1.0f}, Tolerance));
    REQUIRE(GlProbe::Near(image.At(32, 9), {1.0f, 0.0f, 0.0f, 1.0f}, Tolerance));
    INFO("half red, premultiplied, on the left edge");
    REQUIRE(GlProbe::Near(image.At(8, 32), {0.5f, 0.0f, 0.0f, 0.5f}, Tolerance));
    INFO("nothing outside it, nor in the rounded-off corner");
    REQUIRE(GlProbe::Near(image.At(6, 32), Transparent, Tolerance));
    REQUIRE(GlProbe::Near(image.At(10, 10), Transparent, Tolerance));
    REQUIRE(test.scene.messages->errors.empty());
}

TEST_CASE("A radial gradient blends from its centre out to its radius", "[painter][gl]")
{
    GlPainter test;
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, test.Config());
    Fenrir::RadialGradient gradient;
    gradient.centre = {32, 32};
    gradient.radius = 32;
    gradient.focal = {32, 32};
    gradient.stops = {{0.0f, Colour::White}, {1.0f, Colour::Black}};
    painter.DrawRect({0, 0, 64, 64}, gradient);

    const GlProbe::Image image = test.Draw(std::move(list));

    // The stops blend in sRGB: at offset t a pixel is sRGB 1 - t, decoded. Pixel (x, 32)'s centre is (x + 0.5, 32.5).
    const auto expected = [](float x) {
        const float t = std::sqrt((x + 0.5f - 32.0f) * (x + 0.5f - 32.0f) + 0.25f) / 32.0f;
        const float value = Decode(1.0f - std::min(t, 1.0f));
        return GlProbe::Pixel{value, value, value, 1.0f};
    };
    INFO("near white at the centre, sRGB 0.48 halfway out, near black at the edge");
    REQUIRE(GlProbe::Near(image.At(32, 32), expected(32), Tolerance));
    REQUIRE(GlProbe::Near(image.At(48, 32), expected(48), Tolerance));
    REQUIRE(GlProbe::Near(image.At(1, 32), expected(1), Tolerance));
    INFO("and black past the radius, in the corner, padded");
    REQUIRE(GlProbe::Near(image.At(1, 1), {0.0f, 0.0f, 0.0f, 1.0f}, Tolerance));
    REQUIRE(test.scene.messages->errors.empty());
}

TEST_CASE("A bordered box shows each side's colour over its fill, and nothing outside", "[painter][gl]")
{
    GlPainter test;
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, test.Config());
    Fenrir::Border border = Fenrir::Border::Uniform(4, Colour::White);
    border.top = Colour::Red;
    border.left = Colour::Green;
    painter.DrawRect({16, 16, 32, 32}, Colour::Blue);
    painter.DrawBorder({16, 16, 32, 32}, {}, border);

    const GlProbe::Image image = test.Draw(std::move(list));

    INFO("the fill's blue in the middle");
    REQUIRE(GlProbe::Near(image.At(32, 32), {0.0f, 0.0f, 1.0f, 1.0f}, Tolerance));
    INFO("the top side red, the left green, the right and bottom white, each 4 pixels wide");
    REQUIRE(GlProbe::Near(image.At(32, 17), {1.0f, 0.0f, 0.0f, 1.0f}, Tolerance));
    REQUIRE(GlProbe::Near(image.At(17, 32), {0.0f, 1.0f, 0.0f, 1.0f}, Tolerance));
    REQUIRE(GlProbe::Near(image.At(46, 32), {1.0f, 1.0f, 1.0f, 1.0f}, Tolerance));
    REQUIRE(GlProbe::Near(image.At(32, 46), {1.0f, 1.0f, 1.0f, 1.0f}, Tolerance));
    INFO("the fill again just inside the border, and nothing just outside the box");
    REQUIRE(GlProbe::Near(image.At(20, 32), {0.0f, 0.0f, 1.0f, 1.0f}, Tolerance));
    REQUIRE(GlProbe::Near(image.At(15, 32), Transparent, Tolerance));
    REQUIRE(test.scene.messages->errors.empty());
}

namespace
{
    /** @brief Whether some pixel is partly covered: its alpha strictly between 10% and 90% of `full`. */
    bool HasPartialPixel(const GlProbe::Image& image, float full)
    {
        for (std::uint32_t y = 0; y < Size; ++y)
        {
            for (std::uint32_t x = 0; x < Size; ++x)
            {
                const float alpha = image.At(x, y)[3];
                if (alpha > 0.1f * full && alpha < 0.9f * full)
                {
                    return true;
                }
            }
        }
        return false;
    }
} // namespace

TEST_CASE("A stroked star covers its joins once and fades over a pixel at its edges", "[painter][gl]")
{
    GlPainter test;
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, test.Config());
    // Five points 24 out and five inner corners 10 out about the image's centre, the first point straight up.
    std::vector<Fenrir::Math::Vec2> points;
    for (int i = 0; i < 10; ++i)
    {
        const float angle =
            -std::numbers::pi_v<float> / 2.0f + static_cast<float>(i) * std::numbers::pi_v<float> / 5.0f;
        const float radius = i % 2 == 0 ? 24.0f : 10.0f;
        points.push_back({32.0f + radius * std::cos(angle), 32.0f + radius * std::sin(angle)});
    }
    Fenrir::Path star;
    star.AddPolygon(points);
    Fenrir::StrokeStyle style;
    style.width = 4;
    // Half-transparent red: covered once it reads half red, twice it would read three quarters.
    painter.StrokePath(star, style, Colour{1.0f, 0.0f, 0.0f, 0.5f});

    const GlProbe::Image image = test.Draw(std::move(list));

    INFO("half red, premultiplied, at the top point's join and at an inner corner's, where the segments overlap");
    REQUIRE(GlProbe::Near(image.At(32, 8), {0.5f, 0.0f, 0.0f, 0.5f}, Tolerance));
    REQUIRE(GlProbe::Near(image.At(37, 23), {0.5f, 0.0f, 0.0f, 0.5f}, Tolerance));
    INFO("nothing inside the star's stroke, nor outside it");
    REQUIRE(GlProbe::Near(image.At(32, 32), Transparent, Tolerance));
    REQUIRE(GlProbe::Near(image.At(2, 60), Transparent, Tolerance));
    INFO("partly covered pixels along its edges");
    REQUIRE(HasPartialPixel(image, 0.5f));
    INFO("and the renderer logged no error");
    REQUIRE(test.scene.messages->errors.empty());
}

TEST_CASE("A filled ring with a hole is solid in its band, empty in its hole and fades at both edges", "[painter][gl]")
{
    GlPainter test;
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, test.Config());
    Fenrir::Path ring;
    ring.AddCircle({32, 32}, 24).AddCircle({32, 32}, 12).SetFillRule(Fenrir::FillRule::EvenOdd);
    painter.FillPath(ring, Colour::White);

    const GlProbe::Image image = test.Draw(std::move(list));

    INFO("white in the band, 18 from the centre above and below it");
    REQUIRE(GlProbe::Near(image.At(32, 13), {1.0f, 1.0f, 1.0f, 1.0f}, Tolerance));
    REQUIRE(GlProbe::Near(image.At(31, 50), {1.0f, 1.0f, 1.0f, 1.0f}, Tolerance));
    INFO("nothing in the hole or outside the ring");
    REQUIRE(GlProbe::Near(image.At(32, 32), Transparent, Tolerance));
    REQUIRE(GlProbe::Near(image.At(2, 2), Transparent, Tolerance));
    INFO("partly covered pixels along its edges");
    REQUIRE(HasPartialPixel(image, 1.0f));
    // The middle row crosses the edges at x = 8, 20, 44 and 56; the pixel there is about half covered.
    const float outer = image.At(7, 32)[3] + image.At(8, 32)[3];
    INFO("the outer edge's two pixels about x = 8 sum to one pixel's cover (" << outer << ")");
    REQUIRE(std::abs(outer - 1.0f) < 0.15f);
    INFO("and the renderer logged no error");
    REQUIRE(test.scene.messages->errors.empty());
}

namespace
{
    /** @brief The share of a straight edge at `distance` pixels inside (negative) or outside it, blurred by `sigma`. */
    float BlurredEdge(float distance, float sigma)
    {
        return 0.5f * std::erfc(distance / (sigma * std::numbers::sqrt2_v<float>));
    }
} // namespace

TEST_CASE("A half-opacity group draws over a solid colour as one, half and half", "[painter][gl][groups]")
{
    GlPainter test;
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, test.Config());
    painter.DrawRect({0, 0, 64, 64}, Colour::Blue);
    painter.BeginGroup({0, 0, 64, 64}, 0.5f);
    // Two overlapping rectangles: in a group they are one layer, so the overlap is no redder than the rest.
    painter.DrawRect({16, 16, 24, 32}, Colour::Red);
    painter.DrawRect({24, 16, 24, 32}, Colour::Red);
    painter.EndGroup();

    const GlProbe::Image image = test.Draw(std::move(list));

    INFO("half red over blue, where one rectangle is and where both are");
    REQUIRE(GlProbe::Near(image.At(20, 32), {0.5f, 0.0f, 0.5f, 1.0f}, Tolerance));
    REQUIRE(GlProbe::Near(image.At(32, 32), {0.5f, 0.0f, 0.5f, 1.0f}, Tolerance));
    INFO("blue outside them");
    REQUIRE(GlProbe::Near(image.At(4, 4), {0.0f, 0.0f, 1.0f, 1.0f}, Tolerance));
    REQUIRE(test.scene.messages->errors.empty());
}

TEST_CASE("A blurred disc's edge falls off as a Gaussian of the radius", "[painter][gl][groups]")
{
    GlPainter test;
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, test.Config());
    painter.BeginGroup({16, 16, 32, 32}, 1.0f, Fenrir::GroupBlend2D::Normal, Fenrir::GroupFilter::Blur(2.0f));
    painter.DrawCircle({32, 32}, 12, Colour::White);
    painter.EndGroup();

    const GlProbe::Image image = test.Draw(std::move(list));

    // Along the row through the centre the edge is at x = 44; pixel x's centre is x + 0.5. The disc's own one-pixel
    // anti-aliasing and its curve widen the fall-off slightly, which the tolerance takes.
    constexpr float BlurTolerance = 0.04f;
    for (const int x : {32, 40, 42, 44, 46, 48})
    {
        const float expected = BlurredEdge(static_cast<float>(x) + 0.5f - 44.0f, 2.0f);
        INFO("pixel " << x << ": alpha " << image.At(x, 32)[3] << ", expected " << expected);
        REQUIRE(std::abs(image.At(x, 32)[3] - expected) <= BlurTolerance);
    }
    INFO("the blur spreads past the group's bounds by its reach, and no further");
    REQUIRE(image.At(8, 32)[3] == 0.0f);
    REQUIRE(test.scene.messages->errors.empty());
}

TEST_CASE("A masked gradient shows inside the mask's disc and nothing outside it", "[painter][gl][masks]")
{
    GlPainter test;
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, test.Config());
    Fenrir::Path disc;
    disc.AddCircle({32, 32}, 16);
    painter.BeginMask(disc);
    Fenrir::LinearGradient gradient;
    gradient.end = {64, 0};
    gradient.stops = {{0.0f, Colour::Black}, {1.0f, Colour::White}};
    painter.DrawRect({0, 0, 64, 64}, gradient);
    painter.EndMask();

    const GlProbe::Image image = test.Draw(std::move(list));

    // The stops blend in sRGB: at pixel x the gradient is sRGB (x + 0.5) / 64, decoded.
    const auto expected = [](int x) {
        const float value = Decode((static_cast<float>(x) + 0.5f) / 64.0f);
        return GlProbe::Pixel{value, value, value, 1.0f};
    };
    INFO("the gradient inside the disc");
    REQUIRE(GlProbe::Near(image.At(32, 32), expected(32), Tolerance));
    REQUIRE(GlProbe::Near(image.At(22, 32), expected(22), Tolerance));
    REQUIRE(GlProbe::Near(image.At(42, 32), expected(42), Tolerance));
    INFO("nothing outside it, across the gradient and in the corners");
    REQUIRE(GlProbe::Near(image.At(8, 32), Transparent, Tolerance));
    REQUIRE(GlProbe::Near(image.At(56, 32), Transparent, Tolerance));
    REQUIRE(GlProbe::Near(image.At(18, 18), Transparent, Tolerance));
    REQUIRE(test.scene.messages->errors.empty());
}

TEST_CASE("A backdrop blur blurs what is under it, the right way up, and leaves the rest", "[painter][gl][groups]")
{
    GlPainter test;
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, test.Config());
    // Red above y = 32, blue below.
    painter.DrawRect({0, 0, 64, 32}, Colour::Red);
    painter.DrawRect({0, 32, 64, 32}, Colour::Blue);
    painter.BackdropBlur({16, 16, 32, 32}, 2.0f);

    const GlProbe::Image image = test.Draw(std::move(list));

    // The copy reaches six pixels past the panel, so its rows near y = 32 blur as an unbroken edge.
    const float red = BlurredEdge(31.5f - 32.0f, 2.0f);
    INFO("just above the edge, in the panel: mostly red, blurred; just below it, mostly blue");
    REQUIRE(GlProbe::Near(image.At(32, 31), {red, 0.0f, 1.0f - red, 1.0f}, 0.03f));
    REQUIRE(GlProbe::Near(image.At(32, 32), {1.0f - red, 0.0f, red, 1.0f}, 0.03f));
    INFO("outside the panel, on the same edge, unblurred");
    REQUIRE(GlProbe::Near(image.At(8, 31), {1.0f, 0.0f, 0.0f, 1.0f}, Tolerance));
    REQUIRE(GlProbe::Near(image.At(8, 32), {0.0f, 0.0f, 1.0f, 1.0f}, Tolerance));
    INFO("far from the edge in the panel, the colour as it was");
    REQUIRE(GlProbe::Near(image.At(32, 18), {1.0f, 0.0f, 0.0f, 1.0f}, Tolerance));
    REQUIRE(test.scene.messages->errors.empty());
}

namespace
{
    /** @brief Where a word drawn black on white inked the image, and how dark it is by its baseline and above it. */
    struct Inked
    {
        int left = -1;
        int right = -1;
        /** @brief The mean red of the rows just above the baseline, and of a row above the line's ascent. */
        float atBaseline = 1.0f;
        float above = 1.0f;
    };

    /**
     * @brief "Hi" in the embedded face at `size` pixels, black on white from (8, 8), drawn and read back, with the
     * paragraph's own extent: the union of its glyph quads' columns.
     */
    Inked DrawWord(float size, int& predictedLeft, int& predictedRight)
    {
        GlPainter test;
        Fenrir::FontLibrary fonts(test.scene.assets.Store());
        Fenrir::Paragraph paragraph;
        Fenrir::TextStyle style;
        style.size = size;
        style.colour = Colour::Black;
        paragraph.SetText("Hi", style);
        paragraph.Layout(fonts, Fenrir::UnlimitedWidth);
        REQUIRE_FALSE(paragraph.Runs().empty());
        const Fenrir::GlyphRun& run = paragraph.Runs().front();

        float left = 1e9f;
        float right = -1e9f;
        for (const Fenrir::PositionedGlyph& glyph : run.glyphs)
        {
            left = std::min(left, glyph.quad.x);
            right = std::max(right, glyph.quad.x + glyph.quad.width);
        }
        predictedLeft = 8 + static_cast<int>(left);
        predictedRight = 8 + static_cast<int>(right) - 1;

        Fenrir::DrawList2D list;
        Fenrir::Painter painter(list, test.Config());
        painter.DrawRect({0, 0, 64, 64}, Colour::White);
        painter.DrawText(paragraph, {8, 8});
        const GlProbe::Image image = test.Draw(std::move(list));
        REQUIRE(test.scene.messages->errors.empty());

        Inked inked;
        for (std::uint32_t y = 0; y < Size; ++y)
        {
            for (std::uint32_t x = 0; x < Size; ++x)
            {
                if (image.At(x, y)[0] < 0.9f)
                {
                    const int column = static_cast<int>(x);
                    inked.left = inked.left < 0 ? column : std::min(inked.left, column);
                    inked.right = std::max(inked.right, column);
                }
            }
        }
        const auto meanRed = [&](int row) {
            float sum = 0.0f;
            for (int x = inked.left; x <= inked.right; ++x)
            {
                sum += image.At(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(row))[0];
            }
            return sum / static_cast<float>(inked.right - inked.left + 1);
        };
        const int baseline = 8 + static_cast<int>(run.baseline);
        inked.atBaseline = (meanRed(baseline - 1) + meanRed(baseline - 2) + meanRed(baseline - 3)) / 3.0f;
        inked.above = meanRed(8 + static_cast<int>(run.baseline - run.ascent) - 2);
        return inked;
    }
} // namespace

TEST_CASE("A word reads back dark along its baseline and not above it, its extent as the font's metrics say",
          "[painter][gl][text]")
{
    int predictedLeft = 0;
    int predictedRight = 0;
    const Inked small = DrawWord(16.0f, predictedLeft, predictedRight);
    INFO("at 16 pixels: inked columns " << small.left << " to " << small.right << ", the glyph quads' " << predictedLeft
                                        << " to " << predictedRight);
    REQUIRE(small.left >= 0);
    REQUIRE(small.atBaseline < 0.7f);
    REQUIRE(small.above > 0.99f);
    // The ink lies within the glyphs' bitmaps, which reach at most a pixel past it.
    REQUIRE(std::abs(small.left - predictedLeft) <= 1);
    REQUIRE(std::abs(small.right - predictedRight) <= 1);

    const Inked large = DrawWord(32.0f, predictedLeft, predictedRight);
    INFO("at 32 pixels: inked columns " << large.left << " to " << large.right << ", the glyph quads' " << predictedLeft
                                        << " to " << predictedRight);
    REQUIRE(large.atBaseline < 0.7f);
    REQUIRE(large.above > 0.99f);
    REQUIRE(std::abs(large.left - predictedLeft) <= 1);
    REQUIRE(std::abs(large.right - predictedRight) <= 1);
    INFO("twice the size, about twice as wide");
    const int smallWidth = small.right - small.left + 1;
    const int largeWidth = large.right - large.left + 1;
    REQUIRE(std::abs(largeWidth - 2 * smallWidth) <= 3);
}

namespace
{
    /** @brief A row of a glyph scaled up: how many of its pixels are part covered, and its darkest red. */
    struct ScaledRow
    {
        int partial = 0;
        float darkest = 1.0f;
    };

    /**
     * @brief "I" in the embedded face at 12 pixels, in `rendering`'s mode, black on white, scaled by 4 about the
     * target's top-left, drawn and read back along a row through the middle of its stem.
     */
    ScaledRow DrawScaledStem(Fenrir::FontRendering rendering)
    {
        GlPainter test;
        Fenrir::FontLibrary fonts(test.scene.assets.Store());
        fonts.AddFace("Field", fonts.DefaultFont(), Fenrir::FontWeightNormal, Fenrir::FontStyle::Normal,
                      Fenrir::FontRendering::Sdf);
        Fenrir::Paragraph paragraph;
        Fenrir::TextStyle style;
        style.family = rendering == Fenrir::FontRendering::Sdf ? "Field" : "default";
        style.size = 12.0f;
        style.colour = Colour::Black;
        paragraph.SetText("I", style);
        paragraph.Layout(fonts, Fenrir::UnlimitedWidth);
        REQUIRE(paragraph.Runs().size() == 1);
        const Fenrir::GlyphRun& run = paragraph.Runs().front();
        REQUIRE(run.rendering == rendering);

        Fenrir::DrawList2D list;
        Fenrir::Painter painter(list, test.Config());
        painter.DrawRect({0, 0, 64, 64}, Colour::White);
        painter.Scale({4.0f, 4.0f});
        painter.DrawText(paragraph, {2, 0});
        const GlProbe::Image image = test.Draw(std::move(list));
        REQUIRE(test.scene.messages->errors.empty());

        // Four pixels above the baseline at 12 pixels is about the middle of a capital's stem, clear of its serifs.
        const auto row = static_cast<std::uint32_t>(std::lround((run.baseline - 4.0f) * 4.0f));
        ScaledRow scanned;
        for (std::uint32_t x = 0; x < Size; ++x)
        {
            const float red = image.At(x, row)[0];
            scanned.darkest = std::min(scanned.darkest, red);
            scanned.partial += red > 0.1f && red < 0.9f ? 1 : 0;
        }
        return scanned;
    }
} // namespace

TEST_CASE("A glyph scaled by 4 reads back a smooth edge through the SDF path and a blocky one through the bitmap path",
          "[painter][gl][text][sdf]")
{
    const ScaledRow sdf = DrawScaledStem(Fenrir::FontRendering::Sdf);
    INFO("SDF: the stem's row has " << sdf.partial << " part-covered pixels, darkest " << sdf.darkest);
    INFO("the distance field is solid inside the stem and fades over about a screen pixel at each of its two edges");
    REQUIRE(sdf.darkest < 0.05f);
    REQUIRE(sdf.partial >= 1);
    REQUIRE(sdf.partial <= 4);

    const ScaledRow bitmap = DrawScaledStem(Fenrir::FontRendering::Bitmap);
    INFO("bitmap: the stem's row has " << bitmap.partial << " part-covered pixels, darkest " << bitmap.darkest);
    INFO("the 12-pixel bitmap's texels are each stretched over four pixels: its edges spread over its texels");
    REQUIRE(bitmap.partial >= 6);
    REQUIRE(bitmap.partial > 2 * sdf.partial);
}
