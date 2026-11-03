// BindingGroupCache on the null device, through its own interface: one group per key, and the first Get of each frame
// destroying the groups of textures and buffers the device has let go of.

#include <cstdint>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/BindingGroupCache.hpp"
#include "Device/GraphicsDevice.hpp"
#include "FenrirAssets/ShaderContainer.hpp"

using Fenrir::ShaderBindingKind;
using Fenrir::Graphics::BindingGroupEntry;
using Fenrir::Graphics::BindingGroupHandle;
using Fenrir::Graphics::BindingGroupLayoutHandle;
using Fenrir::Graphics::BufferDesc;
using Fenrir::Graphics::BufferHandle;
using Fenrir::Graphics::BufferUsage;
using Fenrir::Graphics::CreateNullDevice;
using Fenrir::Graphics::GraphicsDevice;
using Fenrir::Graphics::SamplerDesc;
using Fenrir::Graphics::SamplerHandle;
using Fenrir::Graphics::TextureDesc;
using Fenrir::Graphics::TextureHandle;
using Fenrir::Render::BindingGroupCache;
using Fenrir::Render::BindingGroupKeyPart;

namespace
{
    /** @brief A null device with a one-texture layout and a sampler, which the cache's groups are made from. */
    struct CacheDevice
    {
        std::unique_ptr<GraphicsDevice> device = CreateNullDevice({.framesInFlight = 2});
        BindingGroupLayoutHandle layout;
        SamplerHandle sampler;
        /** @brief How many groups `Make` has made. */
        int made = 0;

        CacheDevice()
        {
            device->BeginFrame(0);
            layout = device->CreateBindingGroupLayout({{{0, ShaderBindingKind::Texture}}}, "Test");
            sampler = device->CreateSampler(SamplerDesc{}, "Test");
        }
        ~CacheDevice()
        {
            device->Destroy(sampler);
            device->Destroy(layout);
        }
        CacheDevice(const CacheDevice&) = delete;
        CacheDevice& operator=(const CacheDevice&) = delete;
        CacheDevice(CacheDevice&&) = delete;
        CacheDevice& operator=(CacheDevice&&) = delete;

        /** @brief A group binding `texture`, counted in `made`. */
        BindingGroupHandle Make(TextureHandle texture)
        {
            ++made;
            BindingGroupEntry entry;
            entry.binding = 0;
            entry.texture = texture;
            entry.sampler = sampler;
            return device->CreateBindingGroup({layout, {entry}}, "Test");
        }

        TextureHandle Texture()
        {
            return device->CreateTexture(TextureDesc{}, "Test");
        }
    };
} // namespace

TEST_CASE("A binding group cache makes each key's group once in a frame", "[renderer][binding-groups]")
{
    CacheDevice test;
    const TextureHandle texture = test.Texture();
    BindingGroupCache<1> cache(*test.device);
    const BindingGroupHandle first =
        cache.Get({BindingGroupKeyPart::Of(texture)}, 0, [&] { return test.Make(texture); });
    const BindingGroupHandle again =
        cache.Get({BindingGroupKeyPart::Of(texture)}, 0, [&] { return test.Make(texture); });
    INFO("the second Get returns the first group, without making another");
    REQUIRE(again == first);
    REQUIRE(test.made == 1);
    cache.Clear();
    test.device->Destroy(texture);
}

TEST_CASE("A binding group cache keeps a live texture's group from frame to frame", "[renderer][binding-groups]")
{
    CacheDevice test;
    const TextureHandle texture = test.Texture();
    BindingGroupCache<1> cache(*test.device);
    const BindingGroupHandle first =
        cache.Get({BindingGroupKeyPart::Of(texture)}, 0, [&] { return test.Make(texture); });
    const BindingGroupHandle next =
        cache.Get({BindingGroupKeyPart::Of(texture)}, 1, [&] { return test.Make(texture); });
    INFO("the next frame reuses the group");
    REQUIRE(next == first);
    REQUIRE(test.made == 1);
    REQUIRE(test.device->IsLive(first));
    cache.Clear();
    test.device->Destroy(texture);
}

TEST_CASE("A new frame's first Get destroys the groups of textures and buffers the device let go of",
          "[renderer][binding-groups]")
{
    CacheDevice test;
    const TextureHandle gone = test.Texture();
    const TextureHandle kept = test.Texture();
    const BufferHandle buffer = test.device->CreateBuffer(BufferDesc{256, BufferUsage::Storage}, {}, "Test");
    BindingGroupCache<2> cache(*test.device);
    const BindingGroupHandle textureGroup =
        cache.Get({BindingGroupKeyPart::Of(gone), BindingGroupKeyPart::Value(0)}, 0, [&] { return test.Make(gone); });
    const BindingGroupHandle bufferGroup =
        cache.Get({BindingGroupKeyPart::Of(kept), BindingGroupKeyPart::Of(buffer)}, 0, [&] { return test.Make(kept); });
    test.device->Destroy(gone);
    test.device->Destroy(buffer);

    const BindingGroupHandle again =
        cache.Get({BindingGroupKeyPart::Of(gone), BindingGroupKeyPart::Value(0)}, 0, [&] { return test.Make(gone); });
    INFO("within the same frame, nothing is pruned");
    REQUIRE(again == textureGroup);
    REQUIRE(test.device->IsLive(textureGroup));

    (void)cache.Get({BindingGroupKeyPart::Of(kept), BindingGroupKeyPart::Value(1)}, 1, [&] { return test.Make(kept); });
    INFO("the next frame's first Get destroys the group of the destroyed texture, and of the destroyed buffer");
    REQUIRE(!test.device->IsLive(textureGroup));
    REQUIRE(!test.device->IsLive(bufferGroup));
    cache.Clear();
    test.device->Destroy(kept);
}

TEST_CASE("A binding group cache never counts a value part of its key as dead", "[renderer][binding-groups]")
{
    CacheDevice test;
    const TextureHandle texture = test.Texture();
    BindingGroupCache<1> cache(*test.device);
    const BindingGroupHandle valued = cache.Get({BindingGroupKeyPart::Value(7)}, 0, [&] { return test.Make(texture); });
    (void)cache.Get({BindingGroupKeyPart::Value(8)}, 1, [&] { return test.Make(texture); });
    INFO("a key of only a value survives the next frame's prune");
    REQUIRE(test.device->IsLive(valued));
    REQUIRE(cache.Get({BindingGroupKeyPart::Value(7)}, 1, [&] { return test.Make(texture); }) == valued);
    cache.Clear();
    test.device->Destroy(texture);
}

TEST_CASE("Clearing a binding group cache destroys every group", "[renderer][binding-groups]")
{
    CacheDevice test;
    const TextureHandle texture = test.Texture();
    BindingGroupCache<1> cache(*test.device);
    std::vector<BindingGroupHandle> groups;
    for (std::uint32_t value = 0; value < 3; ++value)
    {
        groups.push_back(cache.Get({BindingGroupKeyPart::Value(value)}, 0, [&] { return test.Make(texture); }));
    }
    cache.Clear();
    INFO("all three groups are gone");
    for (const BindingGroupHandle group : groups)
    {
        REQUIRE(!test.device->IsLive(group));
    }
    test.device->Destroy(texture);
}
