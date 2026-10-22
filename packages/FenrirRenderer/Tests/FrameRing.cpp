#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>

#include "Core/FrameRingBuffer.hpp"
#include "Core/RenderGraph.hpp"
#include "Core/TransientPool.hpp"
#include "Device/GraphicsDevice.hpp"
#include "RendererProbe.hpp"

using Fenrir::RendererProbe;
using Fenrir::Graphics::BufferDesc;
using Fenrir::Graphics::BufferHandle;
using Fenrir::Graphics::BufferUsage;
using Fenrir::Graphics::CreateNullDevice;
using Fenrir::Graphics::TextureDesc;
using Fenrir::Graphics::TextureHandle;
using Fenrir::Render::FrameRingBuffer;
using Fenrir::Render::TransientPool;

TEST_CASE("A per-frame ring buffer advances with the frame index", "[renderer][frames]")
{
    auto device = CreateNullDevice({.framesInFlight = 2});
    device->BeginFrame(0);
    FrameRingBuffer uniforms(*device, BufferDesc{256, BufferUsage::Uniform});

    INFO("two frames in flight give two copies");
    REQUIRE(uniforms.CopyCount() == 2);
    REQUIRE(RendererProbe::LiveBuffers(*device) == 2);
    const BufferHandle frame0 = uniforms.Current(0);
    const BufferHandle frame1 = uniforms.Current(1);
    INFO("consecutive frames write different buffers");
    REQUIRE(frame0 != frame1);
    INFO("frame 2 comes back to frame 0's buffer, and frame 5 to frame 1's");
    REQUIRE(uniforms.Current(2) == frame0);
    REQUIRE(uniforms.Current(5) == frame1);

    const std::array<std::byte, 16> block{};
    INFO("each frame writes its own live copy");
    REQUIRE(uniforms.Write(3, 0, block));
}

TEST_CASE("A per-frame ring buffer rings over as many copies as frames in flight", "[renderer][frames]")
{
    auto device = CreateNullDevice({.framesInFlight = 3});
    device->BeginFrame(0);
    FrameRingBuffer vertices(*device, BufferDesc{1024, BufferUsage::Vertex});

    INFO("three frames in flight give three distinct copies, the fourth frame reusing the first's");
    REQUIRE(vertices.CopyCount() == 3);
    REQUIRE(vertices.Current(0) != vertices.Current(1));
    REQUIRE(vertices.Current(1) != vertices.Current(2));
    REQUIRE(vertices.Current(0) != vertices.Current(2));
    REQUIRE(vertices.Current(3) == vertices.Current(0));
}

TEST_CASE("A destroyed ring buffer's copies are destroyed with it", "[renderer][frames]")
{
    auto device = CreateNullDevice();
    device->BeginFrame(0);
    BufferHandle copy;
    {
        FrameRingBuffer uniforms(*device, BufferDesc{64, BufferUsage::Uniform});
        copy = uniforms.Current(0);
    }

    INFO("no copy is left live");
    REQUIRE(RendererProbe::LiveBuffers(*device) == 0);
    REQUIRE_FALSE(device->IsLive(copy));
}

TEST_CASE("The transient pool rings by frame index, so a frame in flight keeps its transients", "[renderer][frames]")
{
    auto device = CreateNullDevice({.framesInFlight = 2});
    TransientPool pool(device->FramesInFlight());
    TextureDesc desc;
    desc.width = 64;
    desc.height = 32;

    std::array<TextureHandle, 3> taken{};
    for (std::uint64_t frame = 0; frame < taken.size(); ++frame)
    {
        device->BeginFrame(frame);
        pool.BeginFrame(frame);
        taken[frame] = pool.Acquire(*device, desc);
        pool.Release(taken[frame]);
        pool.EndFrame(*device);
    }

    INFO("frame 1 does not take frame 0's texture, which may still be in flight");
    REQUIRE(taken[1] != taken[0]);
    INFO("frame 2 takes frame 0's again");
    REQUIRE(taken[2] == taken[0]);
    REQUIRE(RendererProbe::TransientCount(pool) == 2);
}

TEST_CASE("A transient no pass takes in its frame is destroyed", "[renderer][frames]")
{
    auto device = CreateNullDevice({.framesInFlight = 1});
    TransientPool pool(device->FramesInFlight());
    TextureDesc small;
    small.width = 64;
    TextureDesc resized = small;
    resized.width = 128;

    device->BeginFrame(0);
    pool.BeginFrame(0);
    const TextureHandle before = pool.Acquire(*device, small);
    pool.EndFrame(*device);

    device->BeginFrame(1);
    pool.BeginFrame(1);
    const TextureHandle after = pool.Acquire(*device, resized);
    pool.EndFrame(*device);

    INFO("after a resize the old size's texture is destroyed and the new one kept");
    REQUIRE_FALSE(device->IsLive(before));
    REQUIRE(device->IsLive(after));
    REQUIRE(RendererProbe::TransientCount(pool) == 1);
}
