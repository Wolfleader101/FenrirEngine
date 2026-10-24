#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "FenrirMath/Colour.hpp"

using Fenrir::Colour;

namespace
{
    bool Near(float a, float b)
    {
        return std::abs(a - b) < 1e-5f;
    }
} // namespace

TEST_CASE("FromSrgb decodes black and white to linear black and white", "[math][colour]")
{
    const Colour black = Colour::FromSrgb(0x000000FF);
    INFO("0x000000FF did not decode to linear (0, 0, 0, 1)");
    REQUIRE((black.r == 0.0f && black.g == 0.0f && black.b == 0.0f && black.a == 1.0f));

    const Colour white = Colour::FromSrgb(0xFFFFFFFF);
    INFO("0xFFFFFFFF did not decode to linear (1, 1, 1, 1)");
    REQUIRE((white.r == 1.0f && white.g == 1.0f && white.b == 1.0f && white.a == 1.0f));
}

TEST_CASE("FromSrgb decodes sRGB mid grey to linear light", "[math][colour]")
{
    // 0x80 is 128/255 = 0.50196 in sRGB; the sRGB curve, ((0.50196 + 0.055) / 1.055)^2.4, gives 0.21586 linear.
    const Colour grey = Colour::FromSrgb(0x808080FF);
    INFO("0x808080FF decoded to (" << grey.r << ", " << grey.g << ", " << grey.b << "), not linear 0.21586 each");
    REQUIRE((Near(grey.r, 0.21586f) && Near(grey.g, 0.21586f) && Near(grey.b, 0.21586f)));
}

TEST_CASE("FromSrgb decodes each channel from its own byte, below and above the curve's linear segment",
          "[math][colour]")
{
    // 0x08 is 8/255 = 0.031373, under the 0.04045 knee, so it is divided by 12.92: 0.0024282.
    // 0xC0 is 192/255 = 0.75294, so ((0.75294 + 0.055) / 1.055)^2.4 = 0.52712.
    const Colour colour = Colour::FromSrgb(0x08C000FF);
    INFO("0x08C000FF decoded to (" << colour.r << ", " << colour.g << ", " << colour.b
                                   << "), not linear (0.0024282, 0.52712, 0)");
    REQUIRE((Near(colour.r, 0.0024282f) && Near(colour.g, 0.52712f) && colour.b == 0.0f));
}

TEST_CASE("FromSrgb keeps alpha linear: it is not run through the sRGB curve", "[math][colour]")
{
    // Alpha 0x80 is 128/255 = 0.50196; through the sRGB curve it would be 0.21586.
    const Colour colour = Colour::FromSrgb(0xFFFFFF80);
    INFO("alpha 0x80 decoded to " << colour.a << ", not 128/255 = 0.50196");
    REQUIRE(Near(colour.a, 0.50196f));
}

TEST_CASE("The named colours are their linear values, and grey is sRGB mid grey", "[math][colour]")
{
    const auto is = [](const Colour& colour, float r, float g, float b) {
        return Near(colour.r, r) && Near(colour.g, g) && Near(colour.b, b) && colour.a == 1.0f;
    };
    INFO("a named primary or secondary colour is not its opaque linear value");
    REQUIRE(is(Colour::White, 1, 1, 1));
    REQUIRE(is(Colour::Black, 0, 0, 0));
    REQUIRE(is(Colour::Red, 1, 0, 0));
    REQUIRE(is(Colour::Green, 0, 1, 0));
    REQUIRE(is(Colour::Blue, 0, 0, 1));
    REQUIRE(is(Colour::Yellow, 1, 1, 0));
    REQUIRE(is(Colour::Cyan, 0, 1, 1));
    REQUIRE(is(Colour::Magenta, 1, 0, 1));
    INFO("Grey is not sRGB 0x808080 decoded (0.21586 linear)");
    REQUIRE(is(Colour::Grey, 0.21586f, 0.21586f, 0.21586f));
}

TEST_CASE("A default Colour is opaque white", "[math][colour]")
{
    const Colour colour;
    INFO("a default Colour is not (1, 1, 1, 1)");
    REQUIRE((colour.r == 1.0f && colour.g == 1.0f && colour.b == 1.0f && colour.a == 1.0f));
}
