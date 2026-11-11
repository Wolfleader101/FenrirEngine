#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "AssetsTest.hpp"

using AssetsTest::AssetsApp;

namespace
{
    // A flat R8 image of `width` by `height`, every pixel zero.
    Fenrir::ImageData Coverage(std::uint32_t width, std::uint32_t height)
    {
        Fenrir::ImageData image;
        image.width = width;
        image.height = height;
        image.format = Fenrir::ImageFormat::R8;
        image.colourSpace = Fenrir::ColourSpace::Linear;
        image.pixels.assign(std::size_t{width} * height, std::byte{0});
        return image;
    }

    std::vector<std::byte> Filled(std::size_t count, std::uint8_t value)
    {
        return std::vector<std::byte>(count, std::byte{value});
    }
} // namespace

TEST_CASE("An R8 image holds one byte a pixel", "[assets][images][updates]")
{
    AssetsApp test;
    INFO("an R8 pixel is one byte");
    REQUIRE(Fenrir::BytesPerPixel(Fenrir::ImageFormat::R8) == 1);
    INFO("a 4 by 2 R8 image with 8 bytes of pixels is taken");
    REQUIRE_FALSE(test.store.Add<Fenrir::Image>(Coverage(4, 2), Fenrir::AssetOwner::App()).IsEmpty());
}

TEST_CASE("UpdateImage writes the region's rows and bumps the revision", "[assets][images][updates]")
{
    AssetsApp test;
    const Fenrir::AssetHandle<Fenrir::Image> image =
        test.store.Add<Fenrir::Image>(Coverage(4, 3), Fenrir::AssetOwner::App());

    // A 2 by 2 region at (1, 1): its first row 10 11, its second 20 21.
    const std::vector<std::byte> pixels = {std::byte{10}, std::byte{11}, std::byte{20}, std::byte{21}};
    INFO("an update inside the image succeeds");
    REQUIRE(test.store.UpdateImage(image, {1, 1, 2, 2}, pixels));
    INFO("the revision moves on by one");
    REQUIRE(test.store.Revision(image) == 2);

    const std::vector<std::byte>& stored = test.store.TryGet(image)->pixels;
    INFO("row 1 holds the region's first row at columns 1 and 2, and nothing else changed");
    REQUIRE(stored == std::vector<std::byte>{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},   //
                                             std::byte{0}, std::byte{10}, std::byte{11}, std::byte{0}, //
                                             std::byte{0}, std::byte{20}, std::byte{21}, std::byte{0}});
}

TEST_CASE("ImageUpdatesSince gives exactly the regions written after a revision", "[assets][images][updates]")
{
    AssetsApp test;
    const Fenrir::AssetHandle<Fenrir::Image> image =
        test.store.Add<Fenrir::Image>(Coverage(8, 8), Fenrir::AssetOwner::App());
    REQUIRE(test.store.UpdateImage(image, {0, 0, 2, 2}, Filled(4, 1)));
    REQUIRE(test.store.UpdateImage(image, {4, 4, 3, 1}, Filled(3, 2)));

    INFO("a copy at revision 1 takes both regions, oldest first");
    REQUIRE(test.store.ImageUpdatesSince(image, 1) == std::vector<Fenrir::ImageRegion>{{0, 0, 2, 2}, {4, 4, 3, 1}});
    INFO("a copy at revision 2 takes only the second");
    REQUIRE(test.store.ImageUpdatesSince(image, 2) == std::vector<Fenrir::ImageRegion>{{4, 4, 3, 1}});
    INFO("a copy at the current revision takes nothing");
    REQUIRE(test.store.ImageUpdatesSince(image, 3) == std::vector<Fenrir::ImageRegion>{});
    INFO("a revision the image never had means taking it all");
    REQUIRE_FALSE(test.store.ImageUpdatesSince(image, 4).has_value());
}

TEST_CASE("After a replace, a copy from before must take the whole image", "[assets][images][updates]")
{
    AssetsApp test;
    const Fenrir::AssetHandle<Fenrir::Image> image =
        test.store.Add<Fenrir::Image>(Coverage(8, 8), Fenrir::AssetOwner::App());
    REQUIRE(test.store.UpdateImage(image, {0, 0, 1, 1}, Filled(1, 1)));
    REQUIRE(test.store.Replace(image, Coverage(16, 16)));
    REQUIRE(test.store.UpdateImage(image, {2, 2, 1, 1}, Filled(1, 1)));

    INFO("from before the replace, no list of regions catches up");
    REQUIRE_FALSE(test.store.ImageUpdatesSince(image, 1).has_value());
    REQUIRE_FALSE(test.store.ImageUpdatesSince(image, 2).has_value());
    INFO("from the replace's revision, the update after it is enough");
    REQUIRE(test.store.ImageUpdatesSince(image, 3) == std::vector<Fenrir::ImageRegion>{{2, 2, 1, 1}});
}

TEST_CASE("UpdateImage refuses a region outside the image or the wrong pixel count", "[assets][images][updates]")
{
    AssetsApp test;
    const Fenrir::AssetHandle<Fenrir::Image> image =
        test.store.Add<Fenrir::Image>(Coverage(4, 4), Fenrir::AssetOwner::App());

    INFO("a region running past the right edge is refused");
    REQUIRE_FALSE(test.store.UpdateImage(image, {3, 0, 2, 1}, Filled(2, 1)));
    INFO("an empty region is refused");
    REQUIRE_FALSE(test.store.UpdateImage(image, {0, 0, 0, 1}, {}));
    INFO("three bytes for a 2 by 2 region are refused");
    REQUIRE_FALSE(test.store.UpdateImage(image, {0, 0, 2, 2}, Filled(3, 1)));
    INFO("each refusal is logged, and the image is untouched");
    REQUIRE(test.errors->size() == 3);
    REQUIRE(test.store.Revision(image) == 1);
}
