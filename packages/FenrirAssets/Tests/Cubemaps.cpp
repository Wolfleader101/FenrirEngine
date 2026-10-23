// Cubemaps loaded from a `.cubemap` file, which names six face images by face (the spec's seam 4). The fixtures in
// fixtures/cubemap/ are 2 by 2 PNGs: each face's top row is its own colour, its bottom row black.

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "AssetsTest.hpp"

using AssetsTest::AssetsApp;

namespace
{
    using Rgba = std::array<unsigned, 4>;

    // The face colours of the fixtures, in the cubemap's face order: +X, -X, +Y, -Y, +Z, -Z.
    constexpr std::array<Rgba, 6> FaceColours{{
        {255, 0, 0, 255},
        {0, 255, 0, 255},
        {0, 0, 255, 255},
        {255, 255, 0, 255},
        {255, 0, 255, 255},
        {0, 255, 255, 255},
    }};

    // Pixel (x, y) of face `face`, counted from the face's top left.
    Rgba FacePixel(const Fenrir::ImageData& image, std::size_t face, std::size_t x, std::size_t y)
    {
        const std::size_t at = ((face * image.height + y) * image.width + x) * 4;
        Rgba pixel{};
        for (std::size_t c = 0; c < 4; ++c)
        {
            pixel.at(c) = std::to_integer<unsigned>(image.pixels.at(at + c));
        }
        return pixel;
    }

    bool Contains(const std::string& text, std::string_view part)
    {
        return text.find(part) != std::string::npos;
    }
} // namespace

TEST_CASE("A .cubemap file loads its six faces as one cubemap image", "[assets][images][cubemaps]")
{
    AssetsApp test;
    const Fenrir::LoadResult<Fenrir::Image> loaded =
        test.store.Load<Fenrir::Image>("cubemap/sky.cubemap", Fenrir::AssetOwner::App());
    REQUIRE(loaded.Ok());
    const Fenrir::ImageData& image = *test.store.TryGet(loaded.handle);

    INFO("the image is a cubemap of 2 by 2 faces, RGBA8 sRGB as its PNG faces are");
    REQUIRE(image.shape == Fenrir::ImageShape::Cubemap);
    REQUIRE(image.width == 2);
    REQUIRE(image.height == 2);
    REQUIRE(image.format == Fenrir::ImageFormat::RGBA8);
    REQUIRE(image.colourSpace == Fenrir::ColourSpace::Srgb);
    REQUIRE(image.pixels.size() == std::size_t{2} * 2 * 4 * 6);
    for (std::size_t face = 0; face < 6; ++face)
    {
        INFO("face " << face << " holds its file's pixels, top row first");
        REQUIRE(FacePixel(image, face, 1, 0) == FaceColours.at(face));
        REQUIRE(FacePixel(image, face, 0, 1) == Rgba{0, 0, 0, 255});
    }
    REQUIRE(test.errors->empty());
}

TEST_CASE("A .cubemap file's face names put its faces in order, whatever order it lists them",
          "[assets][images][cubemaps]")
{
    AssetsApp test;
    const Fenrir::LoadResult<Fenrir::Image> loaded =
        test.store.Load<Fenrir::Image>("cubemap/shuffled.cubemap", Fenrir::AssetOwner::App());
    REQUIRE(loaded.Ok());
    const Fenrir::ImageData& image = *test.store.TryGet(loaded.handle);

    for (std::size_t face = 0; face < 6; ++face)
    {
        INFO("face " << face << " is the file named for it, though the file lists -Z first");
        REQUIRE(FacePixel(image, face, 0, 0) == FaceColours.at(face));
    }
}

TEST_CASE("A .cubemap file loaded again gives the same handle", "[assets][images][cubemaps]")
{
    AssetsApp test;
    const auto first = test.store.Load<Fenrir::Image>("cubemap/sky.cubemap", Fenrir::AssetOwner::App());
    const auto second = test.store.Load<Fenrir::Image>("cubemap/sky.cubemap", Fenrir::AssetOwner::App());

    INFO("a cubemap is deduplicated by its path, as any image is");
    REQUIRE(first.Ok());
    REQUIRE(first.handle == second.handle);
}

TEST_CASE("A .cubemap file without all six faces fails to import, naming the missing face",
          "[assets][images][cubemaps][diagnostics]")
{
    AssetsApp test;
    const auto loaded = test.store.Load<Fenrir::Image>("cubemap/five_faces.cubemap", Fenrir::AssetOwner::App());

    INFO("the load fails as an import failure");
    REQUIRE_FALSE(loaded.Ok());
    REQUIRE(loaded.diagnostic->reason == Fenrir::LoadFailure::ImportFailed);
    INFO("the importer's message names the face with no image");
    REQUIRE(Contains(loaded.diagnostic->importerMessage, "-Z"));
    REQUIRE(test.errors->size() == 1);
}

TEST_CASE("A .cubemap file whose faces differ in size fails to import", "[assets][images][cubemaps][diagnostics]")
{
    AssetsApp test;
    const auto loaded = test.store.Load<Fenrir::Image>("cubemap/mixed_sizes.cubemap", Fenrir::AssetOwner::App());

    INFO("the load fails as an import failure, naming the face of another size");
    REQUIRE_FALSE(loaded.Ok());
    REQUIRE(loaded.diagnostic->reason == Fenrir::LoadFailure::ImportFailed);
    REQUIRE(Contains(loaded.diagnostic->importerMessage, "-Z"));
    REQUIRE(Contains(loaded.diagnostic->importerMessage, "big.png"));
}

TEST_CASE("A .cubemap file naming a missing face image fails to import, naming it",
          "[assets][images][cubemaps][diagnostics]")
{
    AssetsApp test;
    const auto loaded = test.store.Load<Fenrir::Image>("cubemap/missing_file.cubemap", Fenrir::AssetOwner::App());

    INFO("the load fails as an import failure whose message names the face's file");
    REQUIRE_FALSE(loaded.Ok());
    REQUIRE(loaded.diagnostic->reason == Fenrir::LoadFailure::ImportFailed);
    REQUIRE(Contains(loaded.diagnostic->importerMessage, "absent.png"));
}
