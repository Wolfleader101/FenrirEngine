#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "AssetsTest.hpp"

using AssetsTest::AssetsApp;

namespace
{
    // The bytes of pixel (x, y), counted from the top left.
    std::span<const std::byte> PixelBytes(const Fenrir::ImageData& image, std::size_t x, std::size_t y)
    {
        const std::size_t size = Fenrir::BytesPerPixel(image.format);
        return std::span<const std::byte>(image.pixels).subspan((y * image.width + x) * size, size);
    }

    std::array<float, 4> FloatPixel(const Fenrir::ImageData& image, std::size_t x, std::size_t y)
    {
        std::array<float, 4> pixel{};
        const std::span<const std::byte> bytes = PixelBytes(image, x, y);
        std::memcpy(pixel.data(), bytes.data(), sizeof pixel);
        return pixel;
    }

    std::array<unsigned, 4> BytePixel(const Fenrir::ImageData& image, std::size_t x, std::size_t y)
    {
        std::array<unsigned, 4> pixel{};
        std::ranges::transform(PixelBytes(image, x, y), pixel.begin(),
                               [](std::byte channel) { return std::to_integer<unsigned>(channel); });
        return pixel;
    }

    Fenrir::ImageData Blank(std::uint32_t width, std::uint32_t height, Fenrir::ImageShape shape)
    {
        Fenrir::ImageData image;
        image.width = width;
        image.height = height;
        image.shape = shape;
        const std::size_t faces = shape == Fenrir::ImageShape::Cubemap ? 6 : 1;
        image.pixels.resize(std::size_t{width} * height * 4 * faces);
        return image;
    }
} // namespace

TEST_CASE("A PNG loads as RGBA8 sRGB at its size, rows top to bottom", "[assets][images]")
{
    AssetsApp test;
    const Fenrir::LoadResult<Fenrir::Image> loaded =
        test.store.Load<Fenrir::Image>("fourbytwo.png", Fenrir::AssetOwner::App());
    REQUIRE(loaded.Ok());
    const Fenrir::ImageData& image = *test.store.TryGet(loaded.handle);

    INFO("the fixture is 4 by 2 pixels");
    REQUIRE(image.width == 4);
    REQUIRE(image.height == 2);
    INFO("a PNG is RGBA8 in sRGB, flat");
    REQUIRE(image.format == Fenrir::ImageFormat::RGBA8);
    REQUIRE(image.colourSpace == Fenrir::ColourSpace::Srgb);
    REQUIRE(image.shape == Fenrir::ImageShape::Flat);
    REQUIRE(image.pixels.size() == 4 * 2 * 4);
    INFO("the top-left pixel is opaque red, the top-right white, the bottom-left black");
    REQUIRE(BytePixel(image, 0, 0) == std::array<unsigned, 4>{255, 0, 0, 255});
    REQUIRE(BytePixel(image, 3, 0) == std::array<unsigned, 4>{255, 255, 255, 255});
    REQUIRE(BytePixel(image, 0, 1) == std::array<unsigned, 4>{0, 0, 0, 255});
    INFO("alpha is kept: the second bottom pixel is half-transparent grey");
    REQUIRE(BytePixel(image, 1, 1) == std::array<unsigned, 4>{128, 128, 128, 128});
}

TEST_CASE("A JPEG loads as RGBA8 sRGB at its size", "[assets][images]")
{
    AssetsApp test;
    const Fenrir::LoadResult<Fenrir::Image> loaded =
        test.store.Load<Fenrir::Image>("threebyfive.jpg", Fenrir::AssetOwner::App());
    REQUIRE(loaded.Ok());
    const Fenrir::ImageData& image = *test.store.TryGet(loaded.handle);

    INFO("the fixture is 3 by 5 pixels");
    REQUIRE(image.width == 3);
    REQUIRE(image.height == 5);
    INFO("a JPEG, which has no alpha, is RGBA8 in sRGB and opaque");
    REQUIRE(image.format == Fenrir::ImageFormat::RGBA8);
    REQUIRE(image.colourSpace == Fenrir::ColourSpace::Srgb);
    REQUIRE(image.pixels.size() == 3 * 5 * 4);
    REQUIRE(BytePixel(image, 2, 4)[3] == 255);
}

TEST_CASE("An HDR image loads as RGBA32F linear at its size", "[assets][images]")
{
    AssetsApp test;
    const Fenrir::LoadResult<Fenrir::Image> loaded =
        test.store.Load<Fenrir::Image>("twobytwo.hdr", Fenrir::AssetOwner::App());
    REQUIRE(loaded.Ok());
    const Fenrir::ImageData& image = *test.store.TryGet(loaded.handle);

    INFO("the fixture is 2 by 2 pixels");
    REQUIRE(image.width == 2);
    REQUIRE(image.height == 2);
    INFO("an HDR image is RGBA32F and linear");
    REQUIRE(image.format == Fenrir::ImageFormat::RGBA32F);
    REQUIRE(image.colourSpace == Fenrir::ColourSpace::Linear);
    REQUIRE(image.pixels.size() == 2 * 2 * 16);
    INFO("values above one survive, and alpha is one: the top-left pixel is (2, 1, 0.5)");
    REQUIRE(FloatPixel(image, 0, 0) == std::array<float, 4>{2.0f, 1.0f, 0.5f, 1.0f});
    REQUIRE(FloatPixel(image, 1, 0) == std::array<float, 4>{4.0f, 4.0f, 4.0f, 1.0f});
}

TEST_CASE("An image with no pixels is accepted as a render target", "[assets][images]")
{
    AssetsApp test;
    Fenrir::ImageData target;
    target.width = 64;
    target.height = 32;

    const Fenrir::AssetHandle<Fenrir::Image> handle = test.store.Add<Fenrir::Image>(target, Fenrir::AssetOwner::App());

    INFO("a size with no pixels is a usable image");
    REQUIRE(test.store.IsValid(handle));
    const Fenrir::ImageData& stored = *test.store.TryGet(handle);
    INFO("it keeps its size and RGBA8 sRGB, and has no pixels");
    REQUIRE(stored.width == 64);
    REQUIRE(stored.height == 32);
    REQUIRE(stored.format == Fenrir::ImageFormat::RGBA8);
    REQUIRE(stored.colourSpace == Fenrir::ColourSpace::Srgb);
    REQUIRE(stored.pixels.empty());

    target.width = 128;
    INFO("resizing a render target is a Replace");
    REQUIRE(test.store.Replace(handle, target));
    REQUIRE(test.store.TryGet(handle)->width == 128);
}

TEST_CASE("A cubemap of six faces of one size is accepted", "[assets][images]")
{
    AssetsApp test;
    const Fenrir::AssetHandle<Fenrir::Image> handle =
        test.store.Add<Fenrir::Image>(Blank(2, 2, Fenrir::ImageShape::Cubemap), Fenrir::AssetOwner::App());

    INFO("six 2 by 2 faces make a cubemap");
    REQUIRE(test.store.IsValid(handle));
    REQUIRE(test.store.TryGet(handle)->shape == Fenrir::ImageShape::Cubemap);
}

TEST_CASE("An image whose pixels do not match its size is refused", "[assets][images]")
{
    AssetsApp test;

    Fenrir::ImageData shortPixels = Blank(4, 4, Fenrir::ImageShape::Flat);
    shortPixels.pixels.pop_back();
    INFO("pixels short of width times height are refused");
    REQUIRE(test.store.Add<Fenrir::Image>(shortPixels, Fenrir::AssetOwner::App()).IsEmpty());

    Fenrir::ImageData oneFace = Blank(2, 2, Fenrir::ImageShape::Flat);
    oneFace.shape = Fenrir::ImageShape::Cubemap;
    INFO("a cubemap with one face's pixels is refused");
    REQUIRE(test.store.Add<Fenrir::Image>(oneFace, Fenrir::AssetOwner::App()).IsEmpty());

    INFO("a cubemap whose faces are not square is refused");
    REQUIRE(
        test.store.Add<Fenrir::Image>(Blank(2, 4, Fenrir::ImageShape::Cubemap), Fenrir::AssetOwner::App()).IsEmpty());

    INFO("an image with no size is refused");
    REQUIRE(test.store.Add<Fenrir::Image>(Fenrir::ImageData{}, Fenrir::AssetOwner::App()).IsEmpty());

    INFO("each refusal is logged");
    REQUIRE(test.errors->size() == 4);
}
