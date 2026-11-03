#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/BindingGroupCache.hpp"
#include "Core/BlockRing.hpp"
#include "Core/RenderGraph.hpp"
#include "Device/GraphicsDevice.hpp"

namespace Fenrir
{
    class ILogger;
} // namespace Fenrir

namespace Fenrir::Render
{
    /**
     * @brief A built-in pipeline that draws a fullscreen triangle: `desc` with no culling and no depth attachment, so
     * the caller sets only its shader, sets, colour formats, blending and samples. When the device cannot make it, logs
     * why under `name` and returns an empty handle.
     */
    [[nodiscard]] Graphics::PipelineHandle MakeFullscreenPipeline(Graphics::GraphicsDevice& device, ILogger& logger,
                                                                  Graphics::PipelineDesc desc, std::string_view name);

    /**
     * @brief The passes of a render feature that draws fullscreen triangles over textures it samples, as bloom and GTAO
     * do: each pass binds a block from a per-frame ring at set 1 and its `Sources` textures at set 2 (bindings 0 up,
     * through one sampler), draws into its colour targets, and records nothing else. It owns the two sets' layouts,
     * the sampler, the ring and each set of sources' group, kept while the textures live.
     *
     * Each frame, Reserve the blocks its passes will take before taking any with Block, then add the passes.
     */
    template <std::size_t Sources>
    class FullscreenPasses
    {
      public:
        /** @brief The binding set of a pass's block. */
        static constexpr std::uint32_t BlockSet = 1;
        /** @brief The binding set of a pass's sources. */
        static constexpr std::uint32_t SourceSet = 2;

        /**
         * @brief The layouts, `sampler` and a ring of blocks of `blockSize` bytes, labelled by `name`; the
         * pipelines' failures are logged to `logger`.
         */
        FullscreenPasses(Graphics::GraphicsDevice& device, ILogger& logger, std::string name,
                         const Graphics::SamplerDesc& sampler, std::size_t blockSize)
            : m_device(device), m_logger(logger), m_name(std::move(name)), m_sourceGroups(device)
        {
            m_blockLayout =
                m_device.CreateBindingGroupLayout({{{0, ShaderBindingKind::UniformBuffer}}}, m_name + " block");
            std::vector<Graphics::BindingLayoutEntry> sources;
            for (std::uint32_t binding = 0; binding < Sources; ++binding)
            {
                sources.push_back({binding, ShaderBindingKind::Texture});
            }
            m_sourceLayout = m_device.CreateBindingGroupLayout({sources}, m_name + " sources");
            m_sampler = m_device.CreateSampler(sampler, m_name + " sampler");
            m_blocks = std::make_unique<BlockRing>(m_device, m_blockLayout, false, blockSize, m_name);
        }
        ~FullscreenPasses()
        {
            m_sourceGroups.Clear();
            m_blocks.reset();
            m_device.Destroy(m_sampler);
            m_device.Destroy(m_sourceLayout);
            m_device.Destroy(m_blockLayout);
        }
        FullscreenPasses(const FullscreenPasses&) = delete;
        FullscreenPasses& operator=(const FullscreenPasses&) = delete;
        FullscreenPasses(FullscreenPasses&&) = delete;
        FullscreenPasses& operator=(FullscreenPasses&&) = delete;

        /**
         * @brief A pipeline running `variant` (of the program `program`) over the two sets into `colourFormats`,
         * blended by `blend` at `samples` samples; empty when `variant` is null or the device cannot make it.
         */
        [[nodiscard]] Graphics::PipelineHandle MakePipeline(const ShaderVariant* variant, std::string_view program,
                                                            std::vector<Graphics::TextureFormat> colourFormats,
                                                            Graphics::BlendMode blend = Graphics::BlendMode::None,
                                                            std::uint32_t samples = 1)
        {
            if (variant == nullptr)
            {
                return {};
            }
            Graphics::PipelineDesc desc;
            desc.shader = variant;
            desc.sets.at(BlockSet) = m_blockLayout;
            desc.sets.at(SourceSet) = m_sourceLayout;
            desc.colourFormats = std::move(colourFormats);
            desc.blend = blend;
            desc.samples = samples;
            return MakeFullscreenPipeline(m_device, m_logger, desc, program);
        }

        /** @brief Room for `blocks` blocks this frame, Block taking them from the first. */
        void Reserve(std::size_t blocks)
        {
            m_blocks->Reserve(blocks);
            m_next = 0;
        }

        /** @brief The group of this frame's next block, holding `bytes`; below Reserve's count. */
        [[nodiscard]] Graphics::BindingGroupHandle Block(std::uint64_t frameIndex, std::span<const std::byte> bytes)
        {
            const std::size_t slot = m_next++;
            m_blocks->Write(frameIndex, slot, bytes);
            return m_blocks->Group(frameIndex, slot);
        }

        /**
         * @brief Adds the pass `name` in `stage`: `pipeline` over `viewport`, with `block` and `sources` bound,
         * sampling `sources` and drawing into each of `targets` loaded by `load`.
         */
        void AddPass(RenderGraph& graph, const char* name, RenderStage stage, Graphics::PipelineHandle pipeline,
                     Graphics::BindingGroupHandle block, std::array<GraphResource, Sources> sources,
                     std::vector<GraphResource> targets, Graphics::LoadOp load, Graphics::PixelRect viewport)
        {
            graph.AddPass(
                name, stage, Graphics::QueueKind::Graphics,
                [=](PassBuilder& pass) {
                    for (const GraphResource source : sources)
                    {
                        pass.Sample(source);
                    }
                    for (const GraphResource target : targets)
                    {
                        pass.ColourAttachment(target, load);
                    }
                },
                [this, pipeline, block, sources, viewport](PassContext& context) {
                    std::array<Graphics::TextureHandle, Sources> textures;
                    for (std::size_t i = 0; i < Sources; ++i)
                    {
                        textures[i] = context.Texture(sources[i]);
                    }
                    Graphics::PassEncoder& encoder = context.Encoder();
                    encoder.SetViewport(viewport);
                    encoder.SetPipeline(pipeline);
                    encoder.SetBindingGroup(BlockSet, block);
                    encoder.SetBindingGroup(SourceSet, SourceGroup(textures, context.FrameIndex()));
                    encoder.Draw({.vertexCount = 3});
                });
        }

      private:
        /** @brief The group binding `textures` as a pass's sources in frame `frameIndex`, made once while they live. */
        [[nodiscard]] Graphics::BindingGroupHandle SourceGroup(
            const std::array<Graphics::TextureHandle, Sources>& textures, std::uint64_t frameIndex)
        {
            typename BindingGroupCache<Sources>::Key key;
            for (std::size_t i = 0; i < Sources; ++i)
            {
                key[i] = BindingGroupKeyPart::Of(textures[i]);
            }
            return m_sourceGroups.Get(key, frameIndex, [&] {
                std::vector<Graphics::BindingGroupEntry> entries(Sources);
                for (std::uint32_t i = 0; i < Sources; ++i)
                {
                    entries[i].binding = i;
                    entries[i].texture = textures[i];
                    entries[i].sampler = m_sampler;
                }
                return m_device.CreateBindingGroup({m_sourceLayout, entries}, m_name + " sources");
            });
        }

        Graphics::GraphicsDevice& m_device;
        ILogger& m_logger;
        /** @brief What the layouts, sampler, ring and groups are labelled by. */
        std::string m_name;
        /** @brief Set 1: the pass's block. */
        Graphics::BindingGroupLayoutHandle m_blockLayout;
        /** @brief Set 2: the textures it reads. */
        Graphics::BindingGroupLayoutHandle m_sourceLayout;
        Graphics::SamplerHandle m_sampler;
        std::unique_ptr<BlockRing> m_blocks;
        /** @brief The next block Block takes this frame. */
        std::size_t m_next = 0;
        /** @brief Each set of sources' group, while its textures live. */
        BindingGroupCache<Sources> m_sourceGroups;
    };
} // namespace Fenrir::Render
