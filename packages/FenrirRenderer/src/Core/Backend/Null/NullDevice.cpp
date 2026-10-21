// SPIKE: the null backend (ADR-0016). Creates handles and keeps the books; draws nothing, needs no window or context.

#include "Device/Device.hpp"
#include "Device/HandlePool.hpp"

namespace Fenrir::Gpu
{
    namespace
    {
        struct Empty
        {
        };

        class NullDevice final : public GraphicsDevice
        {
          public:
            BufferHandle CreateBuffer(const BufferDesc&, std::span<const std::byte>) override
            {
                return m_buffers.Add({});
            }
            void WriteBuffer(BufferHandle, std::size_t, std::span<const std::byte>) override
            {
            }
            TextureHandle CreateTexture(const TextureDesc&) override
            {
                return m_textures.Add({});
            }
            void WriteTexture(TextureHandle, int, std::span<const std::byte>) override
            {
            }
            SamplerHandle CreateSampler(const SamplerDesc&) override
            {
                return m_samplers.Add({});
            }
            ShaderHandle CreateShader(const ShaderDesc&, std::string&) override
            {
                return m_shaders.Add({});
            }
            BindGroupLayoutHandle CreateBindGroupLayout(const BindGroupLayoutDesc&) override
            {
                return m_layouts.Add({});
            }
            BindGroupHandle CreateBindGroup(const BindGroupDesc&) override
            {
                return m_groups.Add({});
            }
            PipelineHandle CreatePipeline(const PipelineDesc&) override
            {
                return m_pipelines.Add({});
            }
            void Destroy(BufferHandle h) override
            {
                m_freeBuffers.Queue(h, m_frame);
            }
            void Destroy(TextureHandle h) override
            {
                m_freeTextures.Queue(h, m_frame);
            }
            void Destroy(BindGroupHandle h) override
            {
                m_freeGroups.Queue(h, m_frame);
            }
            void BeginFrame(std::uint64_t frameIndex) override
            {
                m_frame = frameIndex;
                m_stats.draws = 0;
                m_stats.passes = 0;
                m_freeBuffers.Collect(m_frame, 2, [this](BufferHandle h) { m_buffers.Remove(h); });
                m_freeTextures.Collect(m_frame, 2, [this](TextureHandle h) { m_textures.Remove(h); });
                m_freeGroups.Collect(m_frame, 2, [this](BindGroupHandle h) { m_groups.Remove(h); });
            }
            void Submit(const CommandList& list) override
            {
                for (const Command& c : list.commands)
                {
                    if (std::holds_alternative<CmdBeginPass>(c))
                        ++m_stats.passes;
                    else if (std::holds_alternative<CmdDraw>(c) || std::holds_alternative<CmdDrawIndexed>(c))
                        ++m_stats.draws;
                }
            }
            void Present() override
            {
            }
            bool SetSwapInterval(int) override
            {
                return true;
            }
            void GetBackbufferSize(int& w, int& h) override
            {
                w = 640;
                h = 360;
            }
            std::vector<std::uint8_t> ReadBackbuffer(int& w, int& h) override
            {
                w = h = 0;
                return {};
            }
            DeviceStats Stats() const override
            {
                DeviceStats s = m_stats;
                s.pipelines = m_pipelines.LiveCount();
                s.liveBuffers = m_buffers.LiveCount();
                s.liveTextures = m_textures.LiveCount();
                return s;
            }

          private:
            std::uint64_t m_frame = 0;
            DeviceStats m_stats;
            HandlePool<BufferHandle, Empty> m_buffers;
            HandlePool<TextureHandle, Empty> m_textures;
            HandlePool<SamplerHandle, Empty> m_samplers;
            HandlePool<ShaderHandle, Empty> m_shaders;
            HandlePool<BindGroupLayoutHandle, Empty> m_layouts;
            HandlePool<BindGroupHandle, Empty> m_groups;
            HandlePool<PipelineHandle, Empty> m_pipelines;
            DeferredFrees<BufferHandle> m_freeBuffers;
            DeferredFrees<TextureHandle> m_freeTextures;
            DeferredFrees<BindGroupHandle> m_freeGroups;
        };
    } // namespace

    std::unique_ptr<GraphicsDevice> CreateNullDevice()
    {
        return std::make_unique<NullDevice>();
    }
} // namespace Fenrir::Gpu
