#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "Device/GraphicsDevice.hpp"
#include "DeviceTest.hpp"
#include "RendererProbe.hpp"

using Fenrir::RendererProbe;
using namespace Fenrir::Graphics;
using RendererTest::Pass;
using RendererTest::UniformAt;

namespace
{
    const BufferDesc UniformBlock{256, BufferUsage::Uniform};
} // namespace

TEST_CASE("Every kind of device resource is live once made and stale once destroyed", "[renderer][device]")
{
    auto device = CreateNullDevice();
    device->BeginFrame(0);

    const BufferHandle buffer = device->CreateBuffer(UniformBlock);
    const TextureHandle texture = device->CreateTexture(TextureDesc{});
    const SamplerHandle sampler = device->CreateSampler(SamplerDesc{});
    const BindingGroupLayoutHandle layout =
        device->CreateBindingGroupLayout({{{0, Fenrir::ShaderBindingKind::UniformBuffer}}});
    const BindingGroupHandle group = device->CreateBindingGroup({layout, {UniformAt(0, buffer)}});
    const Fenrir::ShaderVariant shader;
    PipelineDesc pipelineDesc;
    pipelineDesc.shader = &shader;
    const PipelineHandle pipeline = device->CreatePipeline(pipelineDesc).pipeline;

    INFO("each new handle names a live resource");
    REQUIRE(device->IsLive(buffer));
    REQUIRE(device->IsLive(texture));
    REQUIRE(device->IsLive(sampler));
    REQUIRE(device->IsLive(layout));
    REQUIRE(device->IsLive(group));
    REQUIRE(device->IsLive(pipeline));

    INFO("each destroy of a live resource succeeds");
    REQUIRE(device->Destroy(pipeline));
    REQUIRE(device->Destroy(group));
    REQUIRE(device->Destroy(layout));
    REQUIRE(device->Destroy(sampler));
    REQUIRE(device->Destroy(texture));
    REQUIRE(device->Destroy(buffer));

    INFO("each destroyed handle is stale at once, though its slot is still held");
    REQUIRE_FALSE(device->IsLive(buffer));
    REQUIRE_FALSE(device->IsLive(texture));
    REQUIRE_FALSE(device->IsLive(sampler));
    REQUIRE_FALSE(device->IsLive(layout));
    REQUIRE_FALSE(device->IsLive(group));
    REQUIRE_FALSE(device->IsLive(pipeline));
}

TEST_CASE("A destroyed handle's slot is freed only once framesInFlight frames have begun", "[renderer][device]")
{
    auto device = CreateNullDevice({.framesInFlight = 2});
    device->BeginFrame(0);
    const BufferHandle destroyed = device->CreateBuffer(UniformBlock);
    REQUIRE(device->Destroy(destroyed));

    INFO("in the frame it was destroyed, its slot waits out the frames in flight");
    REQUIRE(RendererProbe::PendingBuffers(*device) == 1);

    device->BeginFrame(1);
    const BufferHandle madeInFrameOne = device->CreateBuffer(UniformBlock);
    INFO("one frame later, frame 0 may still be in flight: the slot is still held, so a new buffer takes another");
    REQUIRE(RendererProbe::PendingBuffers(*device) == 1);
    REQUIRE(madeInFrameOne.index != destroyed.index);

    device->BeginFrame(2);
    INFO("two frames later the slot is freed");
    REQUIRE(RendererProbe::PendingBuffers(*device) == 0);
    const BufferHandle reused = device->CreateBuffer(UniformBlock);
    INFO("a new buffer reuses the freed slot under a new generation");
    REQUIRE(reused.index == destroyed.index);
    REQUIRE(reused.generation != destroyed.generation);
    INFO("so the old handle stays stale while the new one is live");
    REQUIRE_FALSE(device->IsLive(destroyed));
    REQUIRE(device->IsLive(reused));
}

TEST_CASE("Three frames in flight hold a destroyed slot for three frames", "[renderer][device]")
{
    auto device = CreateNullDevice({.framesInFlight = 3});
    device->BeginFrame(10);
    const TextureHandle texture = device->CreateTexture(TextureDesc{});
    REQUIRE(device->Destroy(texture));
    const std::size_t liveBefore = RendererProbe::LiveTextures(*device);

    device->BeginFrame(12);
    const TextureHandle early = device->CreateTexture(TextureDesc{});
    INFO("two frames later the slot is still held");
    REQUIRE(early.index != texture.index);

    device->BeginFrame(13);
    const TextureHandle later = device->CreateTexture(TextureDesc{});
    INFO("three frames later it is reused");
    REQUIRE(later.index == texture.index);
    INFO("live textures count the two new ones only");
    REQUIRE(RendererProbe::LiveTextures(*device) == liveBefore + 2);
}

TEST_CASE("A stale handle is refused by writes and a second destroy", "[renderer][device]")
{
    auto device = CreateNullDevice();
    device->BeginFrame(0);
    const BufferHandle buffer = device->CreateBuffer(UniformBlock);
    const TextureHandle texture = device->CreateTexture(TextureDesc{});
    const std::array<std::byte, 4> bytes{};

    INFO("a live buffer and texture take writes");
    REQUIRE(device->WriteBuffer(buffer, 0, bytes));
    REQUIRE(device->WriteTexture(texture, 0, 0, bytes));

    REQUIRE(device->Destroy(buffer));
    REQUIRE(device->Destroy(texture));
    INFO("once destroyed, writes through the old handles are refused");
    REQUIRE_FALSE(device->WriteBuffer(buffer, 0, bytes));
    REQUIRE_FALSE(device->WriteTexture(texture, 0, 0, bytes));
    INFO("and destroying them again is refused");
    REQUIRE_FALSE(device->Destroy(buffer));
    REQUIRE_FALSE(device->Destroy(texture));
    INFO("an empty handle is never live");
    REQUIRE_FALSE(device->IsLive(BufferHandle{}));
}

TEST_CASE("The backbuffer is a live texture the caller cannot destroy", "[renderer][device]")
{
    auto device = CreateNullDevice();
    device->BeginFrame(0);
    const TextureHandle backbuffer = device->Backbuffer();

    INFO("the backbuffer is live and the same every frame");
    REQUIRE(device->IsLive(backbuffer));
    device->BeginFrame(1);
    REQUIRE(device->Backbuffer() == backbuffer);
    INFO("destroying it is refused");
    REQUIRE_FALSE(device->Destroy(backbuffer));
    REQUIRE(device->IsLive(backbuffer));
}

TEST_CASE("The null backend accepts any shader container", "[renderer][device]")
{
    auto device = CreateNullDevice();
    device->BeginFrame(0);

    Fenrir::ShaderVariant garbage;
    garbage.stages.push_back({Fenrir::ShaderStage::Vertex, {0xdeadbeefu, 1u, 2u}, {}});
    PipelineDesc desc;
    desc.shader = &garbage;
    const PipelineResult made = device->CreatePipeline(desc);

    INFO("a variant whose code is not SPIR-V still gives a live pipeline, with no diagnostic");
    REQUIRE(device->IsLive(made.pipeline));
    REQUIRE(made.diagnostic.empty());
    REQUIRE(RendererProbe::LivePipelines(*device) == 1);
}

TEST_CASE("A submitted command naming a stale handle is reported", "[renderer][device]")
{
    auto device = CreateNullDevice();
    device->BeginFrame(0);
    const BufferHandle buffer = device->CreateBuffer({64, BufferUsage::Vertex});
    REQUIRE(device->Destroy(buffer));

    CommandList list;
    {
        PassEncoder pass(list, Pass("Stale", device->Backbuffer()));
        pass.SetVertexBuffer(0, buffer);
        pass.Draw({3});
    }
    device->Submit(list);

    INFO("the destroyed vertex buffer is reported once");
    REQUIRE(RendererProbe::StaleUses(*device).size() == 1);
    INFO("the live backbuffer attachment is not");
    REQUIRE(RendererProbe::StaleUses(*device).front().find("buffer") != std::string::npos);
}

TEST_CASE("A readback buffer reads back what was written or copied into it only once that frame has left flight",
          "[renderer][device][culling]")
{
    auto device = CreateNullDevice({.framesInFlight = 2});
    device->BeginFrame(0);
    const BufferHandle readback =
        device->CreateBuffer({16, BufferUsage::CopyDestination | BufferUsage::Readback}, {}, "Readback");
    const std::array<std::uint32_t, 4> written{1, 2, 3, 4};
    REQUIRE(device->WriteBuffer(readback, 0, std::as_bytes(std::span(written))));
    std::array<std::uint32_t, 4> read{};
    const auto readAll = [&] { return device->ReadBuffer(readback, 0, std::as_writable_bytes(std::span(read))); };

    INFO("in the frame it was written, and the next, that frame may still be in flight: refused, reading nothing");
    REQUIRE_FALSE(readAll());
    device->BeginFrame(1);
    REQUIRE_FALSE(readAll());
    REQUIRE(read == std::array<std::uint32_t, 4>{});
    device->BeginFrame(2);
    INFO("two frames later, it reads what was written");
    REQUIRE(readAll());
    REQUIRE(read == written);
    INFO("a part of it from an offset, never a range past its end");
    std::array<std::uint32_t, 2> tail{};
    REQUIRE(device->ReadBuffer(readback, 8, std::as_writable_bytes(std::span(tail))));
    REQUIRE(tail == std::array<std::uint32_t, 2>{3, 4});
    REQUIRE_FALSE(device->ReadBuffer(readback, 8, std::as_writable_bytes(std::span(read))));

    INFO("a copy into it in frame 2 counts as frame 2 writing it: refused until frame 4, which reads the copy");
    const std::array<std::uint32_t, 4> copied{5, 6, 7, 8};
    const BufferHandle source = device->CreateBuffer({16, BufferUsage::CopySource}, std::as_bytes(std::span(copied)));
    CommandList list;
    {
        PassEncoder pass(list, Pass("Copy"));
        pass.CopyBuffer({.source = source, .destination = readback, .size = 16});
    }
    device->Submit(list);
    device->BeginFrame(3);
    REQUIRE_FALSE(readAll());
    device->BeginFrame(4);
    REQUIRE(readAll());
    REQUIRE(read == copied);

    INFO("a pass declaring StorageWrite on it counts as writing it too, though a dispatch leaves its bytes as they "
         "were on the null backend");
    CommandList dispatch;
    {
        PassDesc pass = Pass("Dispatch");
        pass.queue = QueueKind::Compute;
        pass.uses.push_back({{}, readback, ResourceUsage::StorageWrite});
        PassEncoder encoder(dispatch, pass);
        encoder.Dispatch({.groupsX = 1});
    }
    device->Submit(dispatch);
    device->BeginFrame(5);
    REQUIRE_FALSE(readAll());
    device->BeginFrame(6);
    REQUIRE(readAll());
    REQUIRE(read == copied);
    REQUIRE(RendererProbe::StaleUses(*device).empty());
}

TEST_CASE("A buffer made without Readback usage, and a stale handle, are refused a read back however long ago they "
          "were written",
          "[renderer][device][culling]")
{
    auto device = CreateNullDevice({.framesInFlight = 2});
    device->BeginFrame(0);
    const std::array<std::uint32_t, 4> written{1, 2, 3, 4};
    const BufferHandle plain = device->CreateBuffer({16, BufferUsage::Storage}, std::as_bytes(std::span(written)));
    const BufferHandle readback =
        device->CreateBuffer({16, BufferUsage::Readback}, std::as_bytes(std::span(written)), "Readback");
    REQUIRE(device->Destroy(readback));
    device->BeginFrame(5);
    std::array<std::uint32_t, 4> read{};
    INFO("Storage alone is no readback");
    REQUIRE_FALSE(device->ReadBuffer(plain, 0, std::as_writable_bytes(std::span(read))));
    INFO("a destroyed buffer is stale, whatever it held");
    REQUIRE_FALSE(device->ReadBuffer(readback, 0, std::as_writable_bytes(std::span(read))));
    REQUIRE(read == std::array<std::uint32_t, 4>{});
}
