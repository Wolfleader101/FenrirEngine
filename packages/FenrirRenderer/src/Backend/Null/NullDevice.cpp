#include "Backend/Null/NullDevice.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace Fenrir::Graphics
{
    namespace
    {
        constexpr auto ReleaseNothing = [](auto&) {};
    } // namespace

    NullDevice::NullDevice(const DeviceConfig& config) : m_config(config)
    {
        if (m_config.framesInFlight == 0)
        {
            m_config.framesInFlight = 1;
        }
        m_backbuffer = m_textures.Add({});
    }

    std::uint32_t NullDevice::FramesInFlight() const
    {
        return m_config.framesInFlight;
    }

    DeviceFeatures NullDevice::Features() const
    {
        return m_features;
    }

    void NullDevice::BeginFrame(std::uint64_t frameIndex)
    {
        m_frameIndex = frameIndex;
        m_submitted.clear();
        const std::uint32_t frames = m_config.framesInFlight;
        m_buffers.Collect(frameIndex, frames, ReleaseNothing);
        m_textures.Collect(frameIndex, frames, ReleaseNothing);
        m_samplers.Collect(frameIndex, frames, ReleaseNothing);
        m_layouts.Collect(frameIndex, frames, ReleaseNothing);
        m_groups.Collect(frameIndex, frames, ReleaseNothing);
        m_pipelines.Collect(frameIndex, frames, ReleaseNothing);
    }

    BufferHandle NullDevice::CreateBuffer(const BufferDesc& desc, std::span<const std::byte> initial,
                                          std::string_view label)
    {
        NullBuffer buffer{std::vector<std::byte>(desc.size), desc.usage, std::string(label)};
        std::copy_n(initial.begin(), std::min(initial.size(), desc.size), buffer.bytes.begin());
        return m_buffers.Add(std::move(buffer));
    }

    TextureHandle NullDevice::CreateTexture(const TextureDesc& desc, std::string_view label)
    {
        return m_textures.Add({desc, std::string(label), {}});
    }

    SamplerHandle NullDevice::CreateSampler(const SamplerDesc&, std::string_view)
    {
        return m_samplers.Add({});
    }

    BindingGroupLayoutHandle NullDevice::CreateBindingGroupLayout(const BindingGroupLayoutDesc&, std::string_view)
    {
        return m_layouts.Add({});
    }

    BindingGroupHandle NullDevice::CreateBindingGroup(const BindingGroupDesc& desc, std::string_view label)
    {
        return m_groups.Add({desc, std::string(label)});
    }

    PipelineResult NullDevice::CreatePipeline(const PipelineDesc& desc, std::string_view label)
    {
        // Any shader container is accepted: there is nothing to compile it for.
        NullPipeline pipeline{std::string(label), desc.samples, {}, desc.cull, desc.depth, desc.blend, desc.encodeSrgb};
        if (desc.shader != nullptr)
        {
            pipeline.keys = desc.shader->keys;
        }
        return {m_pipelines.Add(std::move(pipeline)), {}};
    }

    PipelineResult NullDevice::CreateComputePipeline(const ComputePipelineDesc& desc, std::string_view label)
    {
        NullPipeline pipeline;
        pipeline.label = std::string(label);
        if (desc.shader != nullptr)
        {
            pipeline.keys = desc.shader->keys;
        }
        return {m_pipelines.Add(std::move(pipeline)), {}};
    }

    bool NullDevice::WriteBuffer(BufferHandle buffer, std::size_t offset, std::span<const std::byte> bytes)
    {
        NullBuffer* found = m_buffers.Find(buffer);
        if (found == nullptr)
        {
            return false;
        }
        // Bytes past the end are dropped, as a real backend would refuse them.
        if (offset < found->bytes.size())
        {
            std::copy_n(bytes.begin(), std::min(bytes.size(), found->bytes.size() - offset),
                        found->bytes.begin() + static_cast<std::ptrdiff_t>(offset));
        }
        return true;
    }

    bool NullDevice::WriteTexture(TextureHandle texture, std::uint32_t mipLevel, std::uint32_t,
                                  std::span<const std::byte>)
    {
        NullTexture* found = m_textures.Find(texture);
        if (found == nullptr)
        {
            return false;
        }
        if (found->writtenMips.size() <= mipLevel)
        {
            found->writtenMips.resize(mipLevel + 1, false);
        }
        found->writtenMips[mipLevel] = true;
        return true;
    }

    bool NullDevice::Destroy(BufferHandle buffer)
    {
        return m_buffers.Destroy(buffer, m_frameIndex);
    }

    bool NullDevice::Destroy(TextureHandle texture)
    {
        // The backbuffer is the surface's, not the caller's.
        return texture != m_backbuffer && m_textures.Destroy(texture, m_frameIndex);
    }

    bool NullDevice::Destroy(SamplerHandle sampler)
    {
        return m_samplers.Destroy(sampler, m_frameIndex);
    }

    bool NullDevice::Destroy(BindingGroupLayoutHandle layout)
    {
        return m_layouts.Destroy(layout, m_frameIndex);
    }

    bool NullDevice::Destroy(BindingGroupHandle group)
    {
        return m_groups.Destroy(group, m_frameIndex);
    }

    bool NullDevice::Destroy(PipelineHandle pipeline)
    {
        return m_pipelines.Destroy(pipeline, m_frameIndex);
    }

    bool NullDevice::IsLive(BufferHandle buffer) const
    {
        return m_buffers.IsLive(buffer);
    }

    bool NullDevice::IsLive(TextureHandle texture) const
    {
        return m_textures.IsLive(texture);
    }

    bool NullDevice::IsLive(SamplerHandle sampler) const
    {
        return m_samplers.IsLive(sampler);
    }

    bool NullDevice::IsLive(BindingGroupLayoutHandle layout) const
    {
        return m_layouts.IsLive(layout);
    }

    bool NullDevice::IsLive(BindingGroupHandle group) const
    {
        return m_groups.IsLive(group);
    }

    bool NullDevice::IsLive(PipelineHandle pipeline) const
    {
        return m_pipelines.IsLive(pipeline);
    }

    TextureHandle NullDevice::Backbuffer() const
    {
        return m_backbuffer;
    }

    void NullDevice::CheckLive(bool live, const char* what)
    {
        Check(live, (std::string("a submitted command names a stale ") + what).c_str());
    }

    void NullDevice::Check(bool ok, const char* what)
    {
        if (!ok)
        {
            m_staleUses.push_back("frame " + std::to_string(m_frameIndex) + ": " + what);
        }
    }

    void NullDevice::CopyBytes(const CmdCopyBuffer& copy)
    {
        const NullBuffer* source = m_buffers.Find(copy.source);
        NullBuffer* destination = m_buffers.Find(copy.destination);
        if (source == nullptr || destination == nullptr)
        {
            return;
        }
        Check(copy.sourceOffset + copy.size <= source->bytes.size() &&
                  copy.destinationOffset + copy.size <= destination->bytes.size(),
              "a buffer copy runs past the end of its source or destination");
        if (copy.sourceOffset + copy.size <= source->bytes.size() &&
            copy.destinationOffset + copy.size <= destination->bytes.size())
        {
            std::copy_n(source->bytes.begin() + static_cast<std::ptrdiff_t>(copy.sourceOffset),
                        static_cast<std::ptrdiff_t>(copy.size),
                        destination->bytes.begin() + static_cast<std::ptrdiff_t>(copy.destinationOffset));
        }
    }

    void NullDevice::CheckIndirectDraw(const CmdDrawIndexedIndirect& draw, const PassDesc* pass,
                                       std::size_t indexOffset)
    {
        const NullBuffer* buffer = m_buffers.Find(draw.buffer);
        CheckLive(buffer != nullptr, "indirect command buffer");
        if (buffer == nullptr)
        {
            return;
        }
        Check(HasUsage(buffer->usage, BufferUsage::Indirect),
              "an indirect draw reads a buffer made without Indirect usage");
        const bool declared =
            pass != nullptr && std::ranges::any_of(pass->uses, [&](const PassResourceUse& use) {
                return use.texture.IsEmpty() && use.buffer == draw.buffer && use.usage == ResourceUsage::IndirectRead;
            });
        Check(declared, "an indirect draw reads a buffer its pass did not declare IndirectRead on");
        Check(draw.stride >= sizeof(DrawIndexedIndirectCommand) && draw.stride % 4 == 0,
              "an indirect draw's stride is under a command's size or not a multiple of four");
        Check(draw.offset % 4 == 0, "an indirect draw's offset is not a multiple of four");
        Check(draw.offset + std::size_t{draw.drawCount} * draw.stride <= buffer->bytes.size(),
              "an indirect draw's commands run past the end of its buffer");
        // GL has no element-buffer offset to count each command's firstIndex from, so the index buffer is bound at its
        // start for every backend.
        Check(indexOffset == 0, "an indirect draw's index buffer is bound past its start");
    }

    void NullDevice::Submit(const CommandList& list)
    {
        // The bookkeeping a real backend's replay would trip over: every handle a command names must be live, and an
        // indirect draw must read commands its pass declared, inside a buffer made for them.
        const PassDesc* pass = nullptr;
        std::size_t indexOffset = 0;
        for (const Command& command : list.commands)
        {
            std::visit(
                [&](const auto& cmd) {
                    using T = std::decay_t<decltype(cmd)>;
                    if constexpr (std::is_same_v<T, CmdBeginPass>)
                    {
                        pass = &cmd.pass;
                        indexOffset = 0;
                        for (const ColourAttachment& colour : cmd.pass.colour)
                        {
                            CheckLive(m_textures.IsLive(colour.texture), "colour attachment");
                        }
                        if (cmd.pass.depth)
                        {
                            CheckLive(m_textures.IsLive(cmd.pass.depth->texture), "depth attachment");
                        }
                        for (const PassResourceUse& use : cmd.pass.uses)
                        {
                            CheckLive(use.texture.IsEmpty() ? m_buffers.IsLive(use.buffer)
                                                            : m_textures.IsLive(use.texture),
                                      "resource in a pass's declared uses");
                        }
                    }
                    else if constexpr (std::is_same_v<T, CmdSetPipeline>)
                    {
                        CheckLive(m_pipelines.IsLive(cmd.pipeline), "pipeline");
                    }
                    else if constexpr (std::is_same_v<T, CmdSetBindingGroup>)
                    {
                        CheckLive(m_groups.IsLive(cmd.group), "binding group");
                    }
                    else if constexpr (std::is_same_v<T, CmdEndPass>)
                    {
                        pass = nullptr;
                    }
                    else if constexpr (std::is_same_v<T, CmdSetVertexBuffer>)
                    {
                        CheckLive(m_buffers.IsLive(cmd.buffer), "buffer");
                    }
                    else if constexpr (std::is_same_v<T, CmdSetIndexBuffer>)
                    {
                        CheckLive(m_buffers.IsLive(cmd.buffer), "buffer");
                        indexOffset = cmd.offset;
                    }
                    else if constexpr (std::is_same_v<T, CmdDrawIndexedIndirect>)
                    {
                        CheckIndirectDraw(cmd, pass, indexOffset);
                    }
                    else if constexpr (std::is_same_v<T, CmdCopyBuffer>)
                    {
                        CheckLive(m_buffers.IsLive(cmd.source) && m_buffers.IsLive(cmd.destination), "buffer");
                        CopyBytes(cmd);
                    }
                },
                command);
        }
        m_submitted.push_back(list);
    }

    void NullDevice::Present()
    {
        ++m_presents;
    }

    void NullDevice::WaitIdle()
    {
    }

    Vsync NullDevice::SetVsync(Vsync mode)
    {
        return mode;
    }

    std::unique_ptr<GraphicsDevice> CreateNullDevice(const DeviceConfig& config)
    {
        return std::make_unique<NullDevice>(config);
    }
} // namespace Fenrir::Graphics
