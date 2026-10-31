#include "Core/Bloom.hpp"

#include <algorithm>
#include <array>
#include <string_view>

#include "Core/BuiltinShaders.hpp"
#include "FenrirLogger/ILogger.hpp"

namespace Fenrir::Render
{
    namespace
    {
        using namespace Fenrir::Graphics;

        /** @brief The binding sets of the passes' block and source texture. */
        constexpr std::uint32_t BlockSet = 1;
        constexpr std::uint32_t SourceSet = 2;

        /** @brief A pass's block, as `builtin:bloom-downsample` and `builtin:bloom-upsample` declare it (std140). */
        struct BloomBlock
        {
            /** @brief One over the source texture's size: a texel, in UV. */
            Math::Vec2 sourceTexel{1.0f};
            /** @brief The upsample's alpha, which the pipeline blends over the target by. */
            float blend = 1.0f;
            /** @brief Whether the downsample takes the Karis average. */
            std::uint32_t karis = 0;
        };
        static_assert(sizeof(BloomBlock) == 16);

        std::span<const std::byte> BytesOf(const BloomBlock& block)
        {
            return std::as_bytes(std::span(&block, 1));
        }

        ShaderContainer ReadProgram(BuiltinProgram program, ILogger& logger)
        {
            ShaderContainerResult read = ReadShaderContainer(GetBuiltinShader(program).bytes);
            if (!read.Ok())
            {
                logger.Error("FenrirRenderer: the built-in shader '{}' does not read: {}",
                             GetBuiltinShader(program).name, read.diagnostic);
                return {};
            }
            return std::move(*read.container);
        }

        /** @brief Level `level`'s size: the view's halved `level + 1` times, at least one pixel. */
        PixelSize LevelSize(PixelSize view, std::uint32_t level)
        {
            return {std::max(view.width >> (level + 1), 1u), std::max(view.height >> (level + 1), 1u)};
        }

        BloomBlock BlockFor(PixelSize source, float blend, bool karis)
        {
            BloomBlock block;
            block.sourceTexel =
                Math::Vec2(1.0f / static_cast<float>(source.width), 1.0f / static_cast<float>(source.height));
            block.blend = blend;
            block.karis = karis ? 1u : 0u;
            return block;
        }
    } // namespace

    std::uint32_t BloomLevels(PixelSize size) noexcept
    {
        std::uint32_t levels = 0;
        for (std::uint32_t side = std::min(size.width, size.height); side > 1 && levels < MaxBloomLevels; side /= 2)
        {
            ++levels;
        }
        return levels;
    }

    std::vector<float> BloomLevelWeights(float radius, std::uint32_t levels)
    {
        radius = std::clamp(radius, 0.0f, 1.0f);
        std::vector<float> weights;
        float share = 1.0f;
        for (std::uint32_t level = 0; level < levels; ++level)
        {
            const bool last = level + 1 == levels;
            weights.push_back(last ? share : share * (1.0f - radius));
            share *= radius;
        }
        return weights;
    }

    BloomChain::BloomChain(GraphicsDevice& device, ILogger& logger, std::uint32_t samples)
        : m_device(device), m_logger(logger)
    {
        m_downsample = ReadProgram(BuiltinProgram::BloomDownsample, m_logger);
        m_upsample = ReadProgram(BuiltinProgram::BloomUpsample, m_logger);
        m_blockLayout = m_device.CreateBindingGroupLayout({{{0, ShaderBindingKind::UniformBuffer}}}, "Bloom block");
        m_sourceLayout = m_device.CreateBindingGroupLayout({{{0, ShaderBindingKind::Texture}}}, "Bloom source");

        // Under MSAA the first downsample reads every sample of SceneColor; the rest read single-sampled levels.
        samples = std::max(samples, 1u);
        constexpr std::array<std::string_view, 1> msaa{"MSAA"};
        const ShaderVariant* base = m_downsample.FindVariant({});
        m_firstDownsample =
            MakePipeline(m_downsample, samples > 1 ? m_downsample.FindVariant(msaa) : base, BlendMode::None, 1);
        m_downsamplePipeline = MakePipeline(m_downsample, base, BlendMode::None, 1);
        // Straight alpha blending, the block's alpha the share of the source: (1 - a) of the target and a of the
        // source.
        m_upsamplePipeline = MakePipeline(m_upsample, m_upsample.FindVariant({}), BlendMode::Alpha, 1);
        m_compositePipeline = MakePipeline(m_upsample, m_upsample.FindVariant({}), BlendMode::Alpha, samples);

        // Mirrored at the edges, not clamped: the filters then see each level as one tile of an endless mirrored image,
        // whose mean they keep, so light near an edge is neither lost past it nor piled onto the edge texels.
        SamplerDesc sampler;
        sampler.addressU = AddressMode::MirroredRepeat;
        sampler.addressV = AddressMode::MirroredRepeat;
        sampler.addressW = AddressMode::MirroredRepeat;
        m_sampler = m_device.CreateSampler(sampler, "Bloom sampler");
        m_blocks = std::make_unique<BlockRing>(m_device, m_blockLayout, false, sizeof(BloomBlock), "Bloom");
    }

    BloomChain::~BloomChain()
    {
        for (const auto& [key, group] : m_sourceGroups)
        {
            m_device.Destroy(group);
        }
        m_blocks.reset();
        m_device.Destroy(m_sampler);
        m_device.Destroy(m_firstDownsample);
        m_device.Destroy(m_downsamplePipeline);
        m_device.Destroy(m_upsamplePipeline);
        m_device.Destroy(m_compositePipeline);
        m_device.Destroy(m_sourceLayout);
        m_device.Destroy(m_blockLayout);
    }

    PipelineHandle BloomChain::MakePipeline(const ShaderContainer& container, const ShaderVariant* variant,
                                            BlendMode blend, std::uint32_t samples)
    {
        if (variant == nullptr)
        {
            return {};
        }
        PipelineDesc desc;
        desc.shader = variant;
        desc.sets.at(BlockSet) = m_blockLayout;
        desc.sets.at(SourceSet) = m_sourceLayout;
        desc.cull = CullMode::None;
        desc.colourFormats = {TextureFormat::Rgba16Float};
        desc.depth.test = false;
        desc.depth.write = false;
        desc.depthFormat.reset();
        desc.blend = blend;
        desc.samples = samples;
        const PipelineResult made = m_device.CreatePipeline(desc, container.name);
        if (made.pipeline.IsEmpty())
        {
            m_logger.Error("FenrirRenderer: the built-in pipeline '{}' could not be made: {}", container.name,
                           made.diagnostic);
        }
        return made.pipeline;
    }

    BindingGroupHandle BloomChain::SourceGroup(TextureHandle texture)
    {
        const std::pair<std::uint32_t, std::uint32_t> key{texture.index, texture.generation};
        if (const auto found = m_sourceGroups.find(key); found != m_sourceGroups.end())
        {
            return found->second;
        }
        BindingGroupEntry entry;
        entry.binding = 0;
        entry.texture = texture;
        entry.sampler = m_sampler;
        const BindingGroupHandle group = m_device.CreateBindingGroup({m_sourceLayout, {entry}}, "Bloom source");
        m_sourceGroups.emplace(key, group);
        return group;
    }

    void BloomChain::AddPasses(RenderGraph& graph, std::uint64_t frameIndex, std::span<const BloomView> views)
    {
        // A transient texture's group outlives its frame, so groups of textures the pool has since freed go here.
        for (auto it = m_sourceGroups.begin(); it != m_sourceGroups.end();)
        {
            if (!m_device.IsLive(TextureHandle{it->first.first, it->first.second}))
            {
                m_device.Destroy(it->second);
                it = m_sourceGroups.erase(it);
            }
            else
            {
                ++it;
            }
        }
        if (m_firstDownsample.IsEmpty() || m_downsamplePipeline.IsEmpty() || m_upsamplePipeline.IsEmpty() ||
            m_compositePipeline.IsEmpty())
        {
            return;
        }

        const auto blooms = [](const BloomView& view) {
            return view.settings.intensity > 0.0f && BloomLevels(view.size) > 0;
        };
        // Each blooming view's passes take one block apiece: a downsample per level, and as many upsamples.
        std::size_t slots = 0;
        for (const BloomView& view : views)
        {
            slots += blooms(view) ? std::size_t{2} * BloomLevels(view.size) : 0;
        }
        if (slots == 0)
        {
            return;
        }
        m_blocks->Reserve(slots);

        std::size_t slot = 0;
        const auto addPass = [&](const char* name, PipelineHandle pipeline, GraphResource source, GraphResource target,
                                 LoadOp load, PixelSize sourceSize, PixelSize targetSize, float blend, bool karis) {
            m_blocks->Write(frameIndex, slot, BytesOf(BlockFor(sourceSize, blend, karis)));
            const BindingGroupHandle block = m_blocks->Group(frameIndex, slot);
            ++slot;
            const PixelRect viewport{0, 0, targetSize.width, targetSize.height};
            graph.AddPass(
                name, RenderStage::PostProcess, QueueKind::Graphics,
                [=](PassBuilder& pass) {
                    pass.Sample(source);
                    pass.ColourAttachment(target, load);
                },
                [this, pipeline, block, source, viewport](PassContext& context) {
                    PassEncoder& encoder = context.Encoder();
                    encoder.SetViewport(viewport);
                    encoder.SetPipeline(pipeline);
                    encoder.SetBindingGroup(BlockSet, block);
                    encoder.SetBindingGroup(SourceSet, SourceGroup(context.Texture(source)));
                    encoder.Draw({.vertexCount = 3});
                });
        };

        for (const BloomView& view : views)
        {
            if (!blooms(view))
            {
                continue;
            }
            const std::uint32_t levels = BloomLevels(view.size);
            const float intensity = std::min(view.settings.intensity, 1.0f);
            const float radius = std::clamp(view.settings.radius, 0.0f, 1.0f);

            std::vector<GraphResource> chain;
            for (std::uint32_t level = 0; level < levels; ++level)
            {
                TextureDesc desc;
                desc.format = TextureFormat::Rgba16Float;
                const PixelSize size = LevelSize(view.size, level);
                desc.width = size.width;
                desc.height = size.height;
                desc.usage = TextureUsage::ColourAttachment | TextureUsage::Sampled;
                chain.push_back(graph.CreateTexture("Bloom", desc));
            }

            // Each downsample covers its whole level, so nothing is loaded.
            for (std::uint32_t level = 0; level < levels; ++level)
            {
                const bool first = level == 0;
                addPass("BloomDownsample", first ? m_firstDownsample : m_downsamplePipeline,
                        first ? view.sceneColour : chain[level - 1], chain[level], LoadOp::DontCare,
                        first ? view.size : LevelSize(view.size, level - 1), LevelSize(view.size, level), 1.0f, first);
            }
            for (std::uint32_t level = levels - 1; level-- > 0;)
            {
                addPass("BloomUpsample", m_upsamplePipeline, chain[level + 1], chain[level], LoadOp::Load,
                        LevelSize(view.size, level + 1), LevelSize(view.size, level), radius, false);
            }
            addPass("BloomComposite", m_compositePipeline, chain[0], view.sceneColour, LoadOp::Load,
                    LevelSize(view.size, 0), view.size, intensity, false);
        }
    }
} // namespace Fenrir::Render
