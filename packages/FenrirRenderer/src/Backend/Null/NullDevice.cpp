#include "Backend/Null/NullDevice.hpp"

#include <type_traits>
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

    BufferHandle NullDevice::CreateBuffer(const BufferDesc&, std::span<const std::byte>, std::string_view)
    {
        return m_buffers.Add({});
    }

    TextureHandle NullDevice::CreateTexture(const TextureDesc&, std::string_view)
    {
        return m_textures.Add({});
    }

    SamplerHandle NullDevice::CreateSampler(const SamplerDesc&, std::string_view)
    {
        return m_samplers.Add({});
    }

    BindingGroupLayoutHandle NullDevice::CreateBindingGroupLayout(const BindingGroupLayoutDesc&, std::string_view)
    {
        return m_layouts.Add({});
    }

    BindingGroupHandle NullDevice::CreateBindingGroup(const BindingGroupDesc&, std::string_view)
    {
        return m_groups.Add({});
    }

    PipelineResult NullDevice::CreatePipeline(const PipelineDesc&, std::string_view)
    {
        // Any shader container is accepted: there is nothing to compile it for.
        return {m_pipelines.Add({}), {}};
    }

    bool NullDevice::WriteBuffer(BufferHandle buffer, std::size_t, std::span<const std::byte>)
    {
        return m_buffers.IsLive(buffer);
    }

    bool NullDevice::WriteTexture(TextureHandle texture, std::uint32_t, std::uint32_t, std::span<const std::byte>)
    {
        return m_textures.IsLive(texture);
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
        if (!live)
        {
            m_staleUses.push_back("frame " + std::to_string(m_frameIndex) + ": a submitted command names a stale " +
                                  what);
        }
    }

    void NullDevice::Submit(const CommandList& list)
    {
        // The bookkeeping a real backend's replay would trip over: every handle a command names must be live.
        for (const Command& command : list.commands)
        {
            std::visit(
                [this](const auto& cmd) {
                    using T = std::decay_t<decltype(cmd)>;
                    if constexpr (std::is_same_v<T, CmdBeginPass>)
                    {
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
                    else if constexpr (std::is_same_v<T, CmdSetVertexBuffer> || std::is_same_v<T, CmdSetIndexBuffer>)
                    {
                        CheckLive(m_buffers.IsLive(cmd.buffer), "buffer");
                    }
                    else if constexpr (std::is_same_v<T, CmdCopyBuffer>)
                    {
                        CheckLive(m_buffers.IsLive(cmd.source) && m_buffers.IsLive(cmd.destination), "buffer");
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
