// Images and render targets from C# (spec: "Scripting: the extensions", Fenrir.Assets; "Cameras, render targets and
// sky"): AddRenderTarget makes a pixel-less RGBA8 sRGB image that ResizeRenderTarget resizes; AddImage and ReplaceImage
// take RGBA8 bytes.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>

#include "AssetsScriptingApp.hpp"
#include "FenrirAssets/ImageData.hpp"

using namespace Fenrir;
using namespace AssetsScriptingTests;

TEST_CASE("AddRenderTarget makes a pixel-less RGBA8 sRGB image that ResizeRenderTarget resizes", "[assets][scripting]")
{
    AssetsScriptingApp test;
    const auto target = HandleFrom<Image>(test.Call<std::uint64_t>(ImageCallsType, "AddRenderTarget", 64, 32));
    {
        const ImageData* image = test.Store().TryGet(target);
        INFO("The render target is not a flat 64 x 32 RGBA8 sRGB image with no pixels");
        REQUIRE(image != nullptr);
        REQUIRE(image->width == 64);
        REQUIRE(image->height == 32);
        REQUIRE(image->format == ImageFormat::RGBA8);
        REQUIRE(image->colourSpace == ColourSpace::Srgb);
        REQUIRE(image->shape == ImageShape::Flat);
        REQUIRE(image->pixels.empty());
    }
    {
        INFO("Resizing the render target from C# did not succeed");
        REQUIRE(test.Call<bool>(ImageCallsType, "ResizeRenderTarget", BitsOf(target), 128, 16));
    }
    const ImageData* resized = test.Store().TryGet(target);
    INFO("The resized render target is not 128 x 16, still RGBA8 sRGB with no pixels, at revision 2");
    REQUIRE(resized != nullptr);
    REQUIRE(resized->width == 128);
    REQUIRE(resized->height == 16);
    REQUIRE(resized->format == ImageFormat::RGBA8);
    REQUIRE(resized->colourSpace == ColourSpace::Srgb);
    REQUIRE(resized->pixels.empty());
    REQUIRE(test.Store().Revision(target) == 2);
}

TEST_CASE("A render target needs a side of at least one pixel, and only a render target resizes", "[assets][scripting]")
{
    AssetsScriptingApp test;
    {
        INFO("A render target with no width was added");
        REQUIRE(test.Call<std::uint64_t>(ImageCallsType, "AddRenderTarget", 0, 32) == 0);
    }
    const auto target = HandleFrom<Image>(test.Call<std::uint64_t>(ImageCallsType, "AddRenderTarget", 8, 8));
    {
        INFO("A render target resized to no height");
        REQUIRE_FALSE(test.Call<bool>(ImageCallsType, "ResizeRenderTarget", BitsOf(target), 8, 0));
        REQUIRE(test.Store().TryGet(target)->height == 8);
    }
    const auto picture = HandleFrom<Image>(test.Call<std::uint64_t>(ImageCallsType, "AddImage", 1, 1, 4, 0));
    INFO("An image with pixels was resized as a render target, losing its pixels");
    REQUIRE_FALSE(test.Call<bool>(ImageCallsType, "ResizeRenderTarget", BitsOf(picture), 4, 4));
    REQUIRE(test.Store().TryGet(picture)->pixels.size() == 4);
}

TEST_CASE("C# adds and replaces an RGBA8 sRGB image from its bytes", "[assets][scripting]")
{
    AssetsScriptingApp test;
    // Two pixels: bytes 10 to 17.
    const auto image = HandleFrom<Image>(test.Call<std::uint64_t>(ImageCallsType, "AddImage", 2, 1, 8, 10));
    {
        const ImageData* added = test.Store().TryGet(image);
        INFO("The image is not 2 x 1 RGBA8 sRGB holding bytes 10 to 17");
        REQUIRE(added != nullptr);
        REQUIRE(added->width == 2);
        REQUIRE(added->height == 1);
        REQUIRE(added->format == ImageFormat::RGBA8);
        REQUIRE(added->colourSpace == ColourSpace::Srgb);
        REQUIRE(added->pixels.size() == 8);
        REQUIRE(added->pixels.front() == std::byte{10});
        REQUIRE(added->pixels.back() == std::byte{17});
    }
    {
        INFO("Replacing the image from C# with 1 x 2 pixels, bytes 100 to 107, did not succeed");
        REQUIRE(test.Call<bool>(ImageCallsType, "ReplaceImage", BitsOf(image), 1, 2, 8, 100));
    }
    const ImageData* replaced = test.Store().TryGet(image);
    INFO("The replaced image is not 1 x 2 holding bytes 100 to 107, at revision 2");
    REQUIRE(replaced->width == 1);
    REQUIRE(replaced->height == 2);
    REQUIRE(replaced->pixels.front() == std::byte{100});
    REQUIRE(replaced->pixels.back() == std::byte{107});
    REQUIRE(test.Store().Revision(image) == 2);
}

TEST_CASE("An image whose bytes do not fill its size gives C# the empty handle or false", "[assets][scripting]")
{
    AssetsScriptingApp test;
    {
        INFO("A 2 x 2 image of 4 bytes was added");
        REQUIRE(test.Call<std::uint64_t>(ImageCallsType, "AddImage", 2, 2, 4, 0) == 0);
    }
    const auto image = HandleFrom<Image>(test.Call<std::uint64_t>(ImageCallsType, "AddImage", 1, 1, 4, 0));
    INFO("A 1 x 1 image was replaced with 3 bytes, or changed");
    REQUIRE_FALSE(test.Call<bool>(ImageCallsType, "ReplaceImage", BitsOf(image), 1, 1, 3, 0));
    REQUIRE(test.Store().Revision(image) == 1);
}
