// The golden-image comparison on the CPU (ADR-0049): CIEDE2000 against Sharma, Wu and Dalal's published test pairs,
// sRGB into CIELAB, and a frame matching its golden image until it is deliberately changed. Needs no GPU, so it runs
// wherever the null tests do.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>

#include <catch2/catch_test_macros.hpp>

#include "GoldenImages.hpp"

using RendererTest::CompareWithGolden;
using RendererTest::DeltaE2000;
using RendererTest::GoldenComparison;
using RendererTest::GoldenTolerance;
using RendererTest::Lab;
using RendererTest::Rgba8Image;

namespace
{
    /** @brief A `width` x `height` image of one opaque colour. */
    Rgba8Image Filled(std::uint32_t width, std::uint32_t height, std::uint8_t r, std::uint8_t g, std::uint8_t b)
    {
        Rgba8Image image;
        image.width = width;
        image.height = height;
        for (std::size_t i = 0; i < std::size_t{width} * height; ++i)
        {
            image.rgba.insert(image.rgba.end(), {r, g, b, 255});
        }
        return image;
    }

    /** @brief Sets the pixel `x` from the left and `y` from the top of `image`. */
    void Set(Rgba8Image& image, std::uint32_t x, std::uint32_t y, std::uint8_t r, std::uint8_t g, std::uint8_t b)
    {
        const std::size_t first = (std::size_t{y} * image.width + x) * 4;
        image.rgba.at(first) = r;
        image.rgba.at(first + 1) = g;
        image.rgba.at(first + 2) = b;
    }
} // namespace

TEST_CASE("CIEDE2000 gives Sharma, Wu and Dalal's published differences", "[renderer][golden]")
{
    // Pairs 1, 7 and 17 of the paper's table 1, to its four decimals.
    INFO("pair 1, two blues of equal lightness");
    REQUIRE(std::abs(DeltaE2000({50.0, 2.6772, -79.7751}, {50.0, 0.0, -82.7485}) - 2.0425) < 1e-4);
    INFO("pair 7, a neutral grey against a slightly coloured one");
    REQUIRE(std::abs(DeltaE2000({50.0, 0.0, 0.0}, {50.0, -1.0, 2.0}) - 2.3669) < 1e-4);
    INFO("pair 17, a large difference");
    REQUIRE(std::abs(DeltaE2000({50.0, 2.5, 0.0}, {73.0, 25.0, -18.0}) - 27.1492) < 1e-4);
    INFO("a colour against itself");
    REQUIRE(DeltaE2000({62.0, 10.0, -5.0}, {62.0, 10.0, -5.0}) == 0.0);
}

TEST_CASE("sRGB white, black and mid grey sit on CIELAB's neutral axis", "[renderer][golden]")
{
    constexpr std::uint8_t white[3]{255, 255, 255};
    constexpr std::uint8_t black[3]{0, 0, 0};
    // sRGB 119 is linear 0.1845, so CIELAB lightness 116 x cbrt(0.1845) - 16 = 50.0.
    constexpr std::uint8_t grey[3]{119, 119, 119};
    const Lab w = RendererTest::LabOfSrgb(white);
    const Lab k = RendererTest::LabOfSrgb(black);
    const Lab g = RendererTest::LabOfSrgb(grey);
    INFO("white is L 100 with no colour");
    REQUIRE(std::abs(w.l - 100.0) < 0.01);
    REQUIRE((std::abs(w.a) < 0.01 && std::abs(w.b) < 0.01));
    INFO("black is L 0");
    REQUIRE(std::abs(k.l) < 0.01);
    INFO("sRGB 119 grey is L 50.0 with no colour");
    REQUIRE(std::abs(g.l - 50.0) < 0.1);
    REQUIRE((std::abs(g.a) < 0.01 && std::abs(g.b) < 0.01));
}

TEST_CASE("A frame matches its golden image through differences too small to see", "[renderer][golden]")
{
    const Rgba8Image golden = Filled(32, 16, 120, 80, 40);
    Rgba8Image actual = golden;
    // One step of 8-bit rounding in every pixel, as another driver's arithmetic might give.
    for (std::size_t i = 0; i < actual.rgba.size(); i += 4)
    {
        ++actual.rgba.at(i);
    }
    // And a few pixels of an edge drawn another way.
    for (std::uint32_t x = 0; x < 10; ++x)
    {
        Set(actual, x, 0, 255, 255, 255);
    }

    const GoldenComparison comparison = CompareWithGolden(golden, actual);

    INFO("the rounding differs from no pixel by more than the tolerance; only the ten edge pixels do, "
         << comparison.differingPixels << " counted");
    REQUIRE(comparison.differingPixels == 10);
    INFO("ten differing pixels are within the sixteen the tolerance allows");
    REQUIRE(comparison.Matches(GoldenTolerance{}));
}

TEST_CASE("A deliberately changed image fails its golden image", "[renderer][golden]")
{
    const Rgba8Image golden = Filled(32, 16, 120, 80, 40);
    SECTION("a block of a different colour")
    {
        Rgba8Image actual = golden;
        for (std::uint32_t y = 4; y < 12; ++y)
        {
            for (std::uint32_t x = 8; x < 16; ++x)
            {
                Set(actual, x, y, 40, 80, 120);
            }
        }
        const GoldenComparison comparison = CompareWithGolden(golden, actual);
        INFO("the 64 changed pixels differ");
        REQUIRE(comparison.differingPixels == 64);
        REQUIRE_FALSE(comparison.Matches(GoldenTolerance{}));
        INFO("the difference image shows a changed pixel in magenta and an unchanged one as the dimmed golden");
        REQUIRE(comparison.difference.width == 32);
        REQUIRE(comparison.difference.rgba.at((std::size_t{5} * 32 + 9) * 4) == 255);
        REQUIRE(comparison.difference.rgba.at((std::size_t{5} * 32 + 9) * 4 + 1) == 0);
        REQUIRE(comparison.difference.rgba.at(0) == 30);
    }
    SECTION("the whole frame a little darker, as a changed tonemapper would make it")
    {
        Rgba8Image actual = golden;
        for (std::size_t i = 0; i < actual.rgba.size(); ++i)
        {
            if (i % 4 != 3)
            {
                actual.rgba.at(i) = static_cast<std::uint8_t>(actual.rgba.at(i) - 8);
            }
        }
        const GoldenComparison comparison = CompareWithGolden(golden, actual);
        INFO("every pixel is over the tolerance, the largest difference " << comparison.largestDeltaE);
        REQUIRE(comparison.differingPixels == std::size_t{32} * 16);
        REQUIRE_FALSE(comparison.Matches(GoldenTolerance{}));
    }
    SECTION("another size")
    {
        INFO("an image of another size never matches");
        REQUIRE_FALSE(CompareWithGolden(golden, Filled(16, 16, 120, 80, 40)).Matches(GoldenTolerance{}));
    }
}

TEST_CASE("A PNG written is read back as the same pixels", "[renderer][golden]")
{
    Rgba8Image image = Filled(5, 3, 10, 20, 30);
    Set(image, 4, 2, 250, 0, 125);
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "FenrirGoldenRoundTrip.png";

    INFO("the PNG is written");
    REQUIRE(RendererTest::WritePng(path, image));
    const auto read = RendererTest::ReadPng(path);
    std::filesystem::remove(path);

    INFO("it reads back as 5 x 3 with the same bytes, top row first");
    REQUIRE(read.has_value());
    REQUIRE(read->width == 5);
    REQUIRE(read->height == 3);
    REQUIRE(read->rgba == image.rgba);
}

TEST_CASE("A missing PNG reads as nothing", "[renderer][golden]")
{
    INFO("no image comes from a path with no file");
    REQUIRE_FALSE(RendererTest::ReadPng(std::filesystem::temp_directory_path() / "FenrirNoSuchGolden.png").has_value());
}
