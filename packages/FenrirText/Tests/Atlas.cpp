#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "FenrirText/GlyphAtlas.hpp"
#include "TextTest.hpp"

using TextTest::TextApp;

namespace
{
    // `width` by `height` bytes of coverage, each `value`.
    std::vector<std::byte> Coverage(std::uint32_t width, std::uint32_t height, std::uint8_t value)
    {
        return std::vector<std::byte>(std::size_t{width} * height, std::byte{value});
    }

    bool Overlap(const Fenrir::ImageRegion& a, const Fenrir::ImageRegion& b)
    {
        return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
    }

    std::uint32_t PageSide(const Fenrir::AssetStore& store, const Fenrir::GlyphAtlas& atlas, std::uint32_t page)
    {
        return store.TryGet(atlas.Pages()[page])->width;
    }
} // namespace

TEST_CASE("Packed rectangles never overlap and stay inside their page", "[text][atlas]")
{
    TextApp test;
    Fenrir::GlyphAtlas atlas(test.store, {128, 128, 1});

    std::vector<Fenrir::AtlasRegion> placed;
    for (std::uint32_t i = 0; i < 60; ++i)
    {
        // Sizes that vary, as glyphs do: 3 to 15 wide, 4 to 14 tall.
        const std::uint32_t width = 3 + (i * 7) % 13;
        const std::uint32_t height = 4 + (i * 5) % 11;
        const std::optional<Fenrir::AtlasRegion> region = atlas.Add(width, height, Coverage(width, height, 255));
        INFO("rectangle " << i << " fits in a 128-pixel atlas that may open pages");
        REQUIRE(region.has_value());
        INFO("the region has the size asked for");
        REQUIRE(region->region.width == width);
        REQUIRE(region->region.height == height);
        placed.push_back(*region);
    }

    for (std::size_t i = 0; i < placed.size(); ++i)
    {
        const std::uint32_t side = PageSide(test.store, atlas, placed[i].page);
        INFO("rectangle " << i << " lies inside its " << side << "-pixel page");
        REQUIRE(placed[i].region.x + placed[i].region.width <= side);
        REQUIRE(placed[i].region.y + placed[i].region.height <= side);
        for (std::size_t j = i + 1; j < placed.size(); ++j)
        {
            INFO("rectangles " << i << " and " << j << " on one page do not overlap");
            REQUIRE((placed[i].page != placed[j].page || !Overlap(placed[i].region, placed[j].region)));
        }
    }
}

TEST_CASE("A full page doubles to the limit, then the atlas opens another page", "[text][atlas]")
{
    TextApp test;
    // No padding, so 16-pixel squares tile a 32-pixel page in four and a 64-pixel page in sixteen.
    Fenrir::GlyphAtlas atlas(test.store, {32, 64, 0});
    const std::vector<std::byte> square = Coverage(16, 16, 9);

    for (int i = 0; i < 4; ++i)
    {
        REQUIRE(atlas.Add(16, 16, square).has_value());
    }
    INFO("four squares fill the first 32-pixel page");
    REQUIRE(atlas.Pages().size() == 1);
    REQUIRE(PageSide(test.store, atlas, 0) == 32);

    const std::optional<Fenrir::AtlasRegion> fifth = atlas.Add(16, 16, square);
    INFO("the fifth doubles the page to 64 pixels rather than opening another");
    REQUIRE(atlas.Pages().size() == 1);
    REQUIRE(PageSide(test.store, atlas, 0) == 64);
    REQUIRE(fifth->page == 0);

    for (int i = 5; i < 16; ++i)
    {
        REQUIRE(atlas.Add(16, 16, square)->page == 0);
    }
    INFO("sixteen fill the 64-pixel page, the limit");
    REQUIRE(atlas.Pages().size() == 1);

    const std::optional<Fenrir::AtlasRegion> next = atlas.Add(16, 16, square);
    INFO("the seventeenth opens a second page instead of doubling past the limit");
    REQUIRE(atlas.Pages().size() == 2);
    REQUIRE(next->page == 1);
    REQUIRE(PageSide(test.store, atlas, 0) == 64);
}

TEST_CASE("Doubling a page keeps what it held", "[text][atlas]")
{
    TextApp test;
    Fenrir::GlyphAtlas atlas(test.store, {16, 64, 0});
    const Fenrir::AtlasRegion first = *atlas.Add(16, 16, Coverage(16, 16, 77));
    REQUIRE(atlas.Add(16, 16, Coverage(16, 16, 5)).has_value());

    const Fenrir::ImageData& page = *test.store.TryGet(atlas.Pages()[0]);
    INFO("the page grew to hold the second square");
    REQUIRE(page.width == 32);
    INFO("the first square's coverage is where its region says, after the growth");
    const std::size_t corner = std::size_t{first.region.y} * page.width + first.region.x;
    REQUIRE(page.pixels[corner] == std::byte{77});
    REQUIRE(page.pixels[corner + 15 * page.width + 15] == std::byte{77});
}

TEST_CASE("Each rectangle is uploaded as exactly its region of the page", "[text][atlas]")
{
    TextApp test;
    Fenrir::GlyphAtlas atlas(test.store, {64, 64, 1});
    // The first add makes the page; what follows is uploaded by region.
    REQUIRE(atlas.Add(4, 4, Coverage(4, 4, 1)).has_value());
    const Fenrir::AssetHandle<Fenrir::Image> page = atlas.Pages()[0];
    const std::uint32_t before = test.store.Revision(page);

    std::vector<Fenrir::ImageRegion> added;
    added.push_back(atlas.Add(5, 3, Coverage(5, 3, 2))->region);
    added.push_back(atlas.Add(2, 7, Coverage(2, 7, 3))->region);
    added.push_back(atlas.Add(6, 6, Coverage(6, 6, 4))->region);

    INFO("the image's updates since are the three added regions, in order, padding excluded");
    REQUIRE(test.store.ImageUpdatesSince(page, before) == added);

    const Fenrir::ImageData& data = *test.store.TryGet(page);
    const Fenrir::ImageRegion last = added.back();
    INFO("the last region holds its coverage");
    REQUIRE(data.pixels[std::size_t{last.y} * data.width + last.x] == std::byte{4});
    INFO("the page is R8 coverage");
    REQUIRE(data.format == Fenrir::ImageFormat::R8);
}

TEST_CASE("An atlas refuses a rectangle larger than its biggest page", "[text][atlas]")
{
    TextApp test;
    Fenrir::GlyphAtlas atlas(test.store, {16, 32, 1});
    INFO("a 40-pixel square cannot fit a 32-pixel page");
    REQUIRE_FALSE(atlas.Add(40, 40, Coverage(40, 40, 1)).has_value());
}

TEST_CASE("An atlas refuses coverage of the wrong size", "[text][atlas]")
{
    TextApp test;
    Fenrir::GlyphAtlas atlas(test.store, {16, 32, 1});
    INFO("twelve bytes for a 4 by 4 rectangle are refused");
    REQUIRE_FALSE(atlas.Add(4, 4, Coverage(3, 4, 1)).has_value());
}

TEST_CASE("An atlas assigned another's unloads the pages it held", "[text][atlas]")
{
    TextApp test;
    Fenrir::GlyphAtlas kept(test.store, {16, 16, 0});
    Fenrir::GlyphAtlas given(test.store, {16, 16, 0});
    REQUIRE(kept.Add(4, 4, Coverage(4, 4, 1)).has_value());
    REQUIRE(given.Add(4, 4, Coverage(4, 4, 2)).has_value());
    const Fenrir::AssetHandle<Fenrir::Image> old = kept.Pages()[0];
    const Fenrir::AssetHandle<Fenrir::Image> taken = given.Pages()[0];

    kept = std::move(given);
    INFO("the page the atlas held is freed, not leaked");
    REQUIRE_FALSE(test.store.IsValid(old));
    INFO("it now holds the other atlas's page");
    REQUIRE(kept.Pages().size() == 1);
    REQUIRE(kept.Pages()[0] == taken);
    REQUIRE(test.store.IsValid(taken));
}
