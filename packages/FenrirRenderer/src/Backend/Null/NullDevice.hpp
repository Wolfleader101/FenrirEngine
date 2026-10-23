#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Device/GraphicsDevice.hpp"
#include "Device/ResourceTable.hpp"

namespace Fenrir
{
    struct RendererProbe;
}

namespace Fenrir::Graphics
{
    /**
     * @brief The null backend (ADR-0016): every handle and book a backend keeps, no window, context or GPU, and nothing
     * drawn. It keeps the CommandLists submitted since the frame began, every stale handle a submitted command named,
     * each buffer's bytes, each binding group's bindings, each texture's description and written mip levels, each
     * pipeline's sample count, variant keys and render state, and the labels of buffers, textures, groups and
     * pipelines, for RendererProbe to read.
     */
    class NullDevice final : public GraphicsDevice
    {
      public:
        explicit NullDevice(const DeviceConfig& config);

        [[nodiscard]] std::uint32_t FramesInFlight() const override;
        void BeginFrame(std::uint64_t frameIndex) override;

        [[nodiscard]] BufferHandle CreateBuffer(const BufferDesc& desc, std::span<const std::byte> initial,
                                                std::string_view label) override;
        [[nodiscard]] TextureHandle CreateTexture(const TextureDesc& desc, std::string_view label) override;
        [[nodiscard]] SamplerHandle CreateSampler(const SamplerDesc& desc, std::string_view label) override;
        [[nodiscard]] BindingGroupLayoutHandle CreateBindingGroupLayout(const BindingGroupLayoutDesc& desc,
                                                                        std::string_view label) override;
        [[nodiscard]] BindingGroupHandle CreateBindingGroup(const BindingGroupDesc& desc,
                                                            std::string_view label) override;
        [[nodiscard]] PipelineResult CreatePipeline(const PipelineDesc& desc, std::string_view label) override;

        bool WriteBuffer(BufferHandle buffer, std::size_t offset, std::span<const std::byte> bytes) override;
        bool WriteTexture(TextureHandle texture, std::uint32_t mipLevel, std::uint32_t layer,
                          std::span<const std::byte> texels) override;

        bool Destroy(BufferHandle buffer) override;
        bool Destroy(TextureHandle texture) override;
        bool Destroy(SamplerHandle sampler) override;
        bool Destroy(BindingGroupLayoutHandle layout) override;
        bool Destroy(BindingGroupHandle group) override;
        bool Destroy(PipelineHandle pipeline) override;

        [[nodiscard]] bool IsLive(BufferHandle buffer) const override;
        [[nodiscard]] bool IsLive(TextureHandle texture) const override;
        [[nodiscard]] bool IsLive(SamplerHandle sampler) const override;
        [[nodiscard]] bool IsLive(BindingGroupLayoutHandle layout) const override;
        [[nodiscard]] bool IsLive(BindingGroupHandle group) const override;
        [[nodiscard]] bool IsLive(PipelineHandle pipeline) const override;

        [[nodiscard]] TextureHandle Backbuffer() const override;
        void Submit(const CommandList& list) override;
        void Present() override;
        void WaitIdle() override;
        /** @copydoc GraphicsDevice::SetVsync With no display to wait for, every mode is supported. */
        Vsync SetVsync(Vsync mode) override;

      private:
        friend struct Fenrir::RendererProbe;

        /** @brief Nothing: the null backend holds no object behind most handles. */
        struct Nothing
        {
        };

        /** @brief A buffer's bytes as last written, and its label, for RendererProbe. */
        struct NullBuffer
        {
            std::vector<std::byte> bytes;
            std::string label;
        };

        /** @brief What a binding group binds, and its label, for RendererProbe. */
        struct NullGroup
        {
            BindingGroupDesc desc;
            std::string label;
        };

        /** @brief A texture's description and label, and which of its mip levels were written, for RendererProbe. */
        struct NullTexture
        {
            TextureDesc desc;
            std::string label;
            /** @brief By mip level: whether WriteTexture wrote it (any layer). */
            std::vector<bool> writtenMips;
        };

        /** @brief A pipeline's label, sample count, shader variant keys and render state, for RendererProbe. */
        struct NullPipeline
        {
            std::string label;
            std::uint32_t samples = 1;
            std::vector<std::string> keys;
            CullMode cull = CullMode::Back;
            DepthState depth;
            BlendMode blend = BlendMode::None;
        };

        void CheckLive(bool live, const char* what);

        DeviceConfig m_config;
        std::uint64_t m_frameIndex = 0;
        ResourceTable<BufferHandle, NullBuffer> m_buffers;
        ResourceTable<TextureHandle, NullTexture> m_textures;
        ResourceTable<SamplerHandle, Nothing> m_samplers;
        ResourceTable<BindingGroupLayoutHandle, Nothing> m_layouts;
        ResourceTable<BindingGroupHandle, NullGroup> m_groups;
        ResourceTable<PipelineHandle, NullPipeline> m_pipelines;
        TextureHandle m_backbuffer;
        /** @brief What Submit received since the frame began, in order. */
        std::vector<CommandList> m_submitted;
        /** @brief One line per stale or empty handle a submitted command named, since the device was made. */
        std::vector<std::string> m_staleUses;
        std::uint64_t m_presents = 0;
    };
} // namespace Fenrir::Graphics
