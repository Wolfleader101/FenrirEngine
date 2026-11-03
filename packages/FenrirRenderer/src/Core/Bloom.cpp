#include "Core/Bloom.hpp"

#include <algorithm>
#include <array>
#include <string_view>

#include "Core/BuiltinShaders.hpp"
#include "Core/FullscreenPipeline.hpp"
#include "FenrirLogger/ILogger.hpp"

namespace Fenrir::Render
{
    namespace
    {
        using namespace Fenrir::Graphics;

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

        // Mirrored at the edges, not clamped: the filters then see each level as one tile of an endless mirrored image,
        // whose mean they keep, so light near an edge is neither lost past it nor piled onto the edge texels.
        SamplerDesc MirroredSampler()
        {
            SamplerDesc sampler;
            sampler.addressU = AddressMode::MirroredRepeat;
            sampler.addressV = AddressMode::MirroredRepeat;
            sampler.addressW = AddressMode::MirroredRepeat;
            return sampler;
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
        : m_device(device),
          m_passes(device, logger, "Bloom", RenderStage::PostProcess, MirroredSampler(), sizeof(BloomBlock))
    {
        m_downsample = ReadBuiltin(BuiltinProgram::BloomDownsample, logger);
        m_upsample = ReadBuiltin(BuiltinProgram::BloomUpsample, logger);

        // Under MSAA the first downsample reads every sample of SceneColor; the rest read single-sampled levels.
        samples = std::max(samples, 1u);
        constexpr std::array<std::string_view, 1> msaa{"MSAA"};
        const ShaderVariant* base = m_downsample.FindVariant({});
        const ShaderVariant* first = samples > 1 ? m_downsample.FindVariant(msaa) : base;
        m_firstDownsample = m_passes.MakePipeline(first, m_downsample.name, {TextureFormat::Rgba16Float});
        m_downsamplePipeline = m_passes.MakePipeline(base, m_downsample.name, {TextureFormat::Rgba16Float});
        // Straight alpha blending, the block's alpha the share of the source: (1 - a) of the target and a of the
        // source.
        const ShaderVariant* upsample = m_upsample.FindVariant({});
        m_upsamplePipeline =
            m_passes.MakePipeline(upsample, m_upsample.name, {TextureFormat::Rgba16Float}, BlendMode::Alpha);
        m_compositePipeline =
            m_passes.MakePipeline(upsample, m_upsample.name, {TextureFormat::Rgba16Float}, BlendMode::Alpha, samples);
    }

    BloomChain::~BloomChain()
    {
        m_device.Destroy(m_firstDownsample);
        m_device.Destroy(m_downsamplePipeline);
        m_device.Destroy(m_upsamplePipeline);
        m_device.Destroy(m_compositePipeline);
    }

    void BloomChain::AddPasses(RenderGraph& graph, std::uint64_t frameIndex, std::span<const BloomView> views)
    {
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
        m_passes.Reserve(slots);

        const auto addPass = [&](const char* name, PipelineHandle pipeline, GraphResource source, GraphResource target,
                                 LoadOp load, PixelSize sourceSize, PixelSize targetSize, float blend, bool karis) {
            const BindingGroupHandle block = m_passes.Block(frameIndex, BytesOf(BlockFor(sourceSize, blend, karis)));
            m_passes.AddPass(graph, name, pipeline, block, {source}, {target}, load,
                             {0, 0, targetSize.width, targetSize.height});
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
