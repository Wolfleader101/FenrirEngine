// Sky images (ADR-0047) on the null device, through their own interface: each cubemap or panorama a cube texture, a
// panorama's passes added until they have been recorded and its image's texture freed after, and what release frees.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/RenderGraph.hpp"
#include "Core/SkyImages.hpp"
#include "Core/TransientPool.hpp"
#include "Device/GraphicsDevice.hpp"
#include "RecordingLogger.hpp"
#include "RendererProbe.hpp"

using Fenrir::RendererProbe;
using Fenrir::Graphics::CreateNullDevice;
using Fenrir::Graphics::TextureFormat;
using Fenrir::Graphics::TextureKind;
using Fenrir::Render::AssetKey;
using Fenrir::Render::RenderGraph;
using Fenrir::Render::SkyCube;
using Fenrir::Render::SkyImages;
using Fenrir::Render::TransientPool;
using RendererTest::LoggedMessages;
using RendererTest::RecordingLogger;

namespace
{
    constexpr AssetKey First{1, 1};
    constexpr AssetKey Second{2, 1};

    /** @brief A cubemap of six 2 x 2 RGBA8 faces, all one grey. */
    Fenrir::ImageData Cubemap()
    {
        Fenrir::ImageData image;
        image.width = 2;
        image.height = 2;
        image.shape = Fenrir::ImageShape::Cubemap;
        image.pixels.resize(std::size_t{2} * 2 * 4 * 6, std::byte{200});
        return image;
    }

    /** @brief A flat 8 x 4 RGBA8 panorama, all one grey: a cube texture of 2 x 2 faces and 2 mip levels. */
    Fenrir::ImageData Panorama()
    {
        Fenrir::ImageData image;
        image.width = 8;
        image.height = 4;
        image.pixels.resize(std::size_t{8} * 4 * 4, std::byte{200});
        return image;
    }

    /** @brief Sky images on the null device. */
    struct Images
    {
        std::unique_ptr<Fenrir::Graphics::GraphicsDevice> device = CreateNullDevice({.framesInFlight = 2});
        RecordingLogger logger{std::make_shared<LoggedMessages>()};
        std::unique_ptr<SkyImages> images;
        TransientPool pool{2};

        Images()
        {
            device->BeginFrame(0);
            images = std::make_unique<SkyImages>(*device, logger);
        }

        /** @brief How many passes AddPasses adds to a frame's graph, recording them when `record`. */
        std::size_t Frame(std::uint64_t frameIndex, bool record)
        {
            RenderGraph graph;
            images->AddPasses(graph);
            if (record)
            {
                (void)graph.Record(graph.Compile(), *device, pool, frameIndex);
            }
            return graph.PassCount();
        }
    };
} // namespace

TEST_CASE("A prepared Sky cubemap's cube texture is its own, with every mip level", "[renderer][sky][sky-images]")
{
    Images sky;
    sky.images->PrepareCubemap(First, 1, Cubemap());
    const SkyCube cube = sky.images->Cube(First);
    INFO("a cube texture of 2 x 2 faces and 2 mip levels, in the image's format");
    REQUIRE_FALSE(cube.texture.IsEmpty());
    REQUIRE(cube.desc.kind == TextureKind::Cube);
    REQUIRE(cube.desc.width == 2);
    REQUIRE(cube.desc.mipLevels == 2);
    REQUIRE(cube.desc.format == TextureFormat::Rgba8Srgb);
}

TEST_CASE("A prepared panorama's cube texture is RGBA16F, with faces half as wide as the image is high",
          "[renderer][sky][sky-images]")
{
    Images sky;
    sky.images->PreparePanorama(First, 1, Panorama());
    const SkyCube cube = sky.images->Cube(First);
    INFO("a cube texture of 2 x 2 faces and 2 mip levels, before its passes have run");
    REQUIRE_FALSE(cube.texture.IsEmpty());
    REQUIRE(cube.desc.kind == TextureKind::Cube);
    REQUIRE(cube.desc.width == 2);
    REQUIRE(cube.desc.mipLevels == 2);
    REQUIRE(cube.desc.format == TextureFormat::Rgba16Float);
}

TEST_CASE("A panorama's passes are added each frame until they have been recorded", "[renderer][sky][sky-images]")
{
    Images sky;
    sky.images->PreparePanorama(First, 1, Panorama());
    INFO("one pass a face and mip level, while none has been recorded");
    REQUIRE(sky.Frame(0, false) == 12);
    REQUIRE(sky.Frame(1, true) == 12);
    INFO("none once they have been");
    REQUIRE(sky.Frame(2, true) == 0);
}

TEST_CASE("A panorama's image texture is freed the frame after its passes are recorded, its cube texture kept",
          "[renderer][sky][sky-images]")
{
    Images sky;
    sky.images->PreparePanorama(First, 1, Panorama());
    const SkyCube cube = sky.images->Cube(First);
    (void)sky.Frame(0, true);
    const std::size_t textures = RendererProbe::LiveTextures(*sky.device);
    (void)sky.Frame(1, true);
    INFO("the image's texture goes");
    REQUIRE(RendererProbe::LiveTextures(*sky.device) == textures - 1);
    INFO("the cube texture stays");
    REQUIRE(sky.images->Cube(First).texture == cube.texture);
}

TEST_CASE("Releasing a Sky image frees its textures", "[renderer][sky][sky-images]")
{
    Images sky;
    const std::size_t textures = RendererProbe::LiveTextures(*sky.device);
    sky.images->PrepareCubemap(First, 1, Cubemap());
    sky.images->PreparePanorama(Second, 1, Panorama());
    INFO("the cubemap's cube texture, and the panorama's image and cube textures");
    REQUIRE(RendererProbe::LiveTextures(*sky.device) == textures + 3);
    sky.images->ReleaseCubemap(First);
    sky.images->ReleasePanorama(Second);
    INFO("all three go");
    REQUIRE(RendererProbe::LiveTextures(*sky.device) == textures);
    INFO("a released key has no cube texture");
    REQUIRE(sky.images->Cube(First).texture.IsEmpty());
    REQUIRE(sky.images->Cube(Second).texture.IsEmpty());
}

TEST_CASE("A Sky image is current only at the revision it was prepared at", "[renderer][sky][sky-images]")
{
    Images sky;
    sky.images->PrepareCubemap(First, 3, Cubemap());
    sky.images->PreparePanorama(Second, 5, Panorama());
    INFO("each at its own revision, of its own kind");
    REQUIRE(sky.images->IsCubemapCurrent(First, 3));
    REQUIRE(sky.images->IsPanoramaCurrent(Second, 5));
    INFO("not at another revision");
    REQUIRE_FALSE(sky.images->IsCubemapCurrent(First, 4));
    REQUIRE_FALSE(sky.images->IsPanoramaCurrent(Second, 4));
    INFO("not as the other kind");
    REQUIRE_FALSE(sky.images->IsPanoramaCurrent(First, 3));
    REQUIRE_FALSE(sky.images->IsCubemapCurrent(Second, 5));
}

TEST_CASE("Sky images list their keys by kind", "[renderer][sky][sky-images]")
{
    Images sky;
    sky.images->PrepareCubemap(First, 1, Cubemap());
    sky.images->PreparePanorama(Second, 1, Panorama());
    INFO("the cubemap's key among the cubemaps, the panorama's among the panoramas");
    REQUIRE(sky.images->CubemapKeys() == std::vector<AssetKey>{First});
    REQUIRE(sky.images->PanoramaKeys() == std::vector<AssetKey>{Second});
}

TEST_CASE("A key with no prepared Sky image has no cube texture", "[renderer][sky][sky-images]")
{
    Images sky;
    INFO("an empty texture for a key never prepared");
    REQUIRE(sky.images->Cube(First).texture.IsEmpty());
}
