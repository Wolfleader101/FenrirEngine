// builtin:painter on OpenGL (the spec's GL check): a rounded rectangle, a radial gradient, a bordered box, a stroked
// star and a filled ring with a hole painted into an image 2D target (ADR-0060) through the renderer's ImageTargets2D,
// and read back at their centres, joins and edges. The image is linear RGBA8, cleared transparent each frame it is
// drawn, and the root's blend space Linear, so a pixel is the shader's premultiplied linear output to 8 bits; the
// expected values are worked by hand.

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
    REQUIRE(test.scene.messages->errors.empty());
}
