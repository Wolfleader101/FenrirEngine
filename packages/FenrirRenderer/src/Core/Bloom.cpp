#include "Core/Bloom.hpp"

#include <algorithm>
#include <array>
#include <string_view>

#include "Core/BuiltinShaders.hpp"
#include "Core/ColourGrading.hpp"
#include "Core/FullscreenPasses.hpp"
#include "Core/LensFlare.hpp"
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
            /** @brief What the upsample multiplies its result by: the streaks' tint into SceneColor, else white. */
            Math::Vec3 tint{1.0f};
            /** @brief The DIRT composite's dirt intensity. */
            float dirtIntensity = 0.0f;
        };
        static_assert(sizeof(BloomBlock) == 32);

        /** @brief A FlareGhosts pass's block, as `builtin:lens-flare` declares it (std140), in a bloom block's room. */
        struct FlareBlock
        {
            float ghostSpacing = 0.4f;
            float haloRadius = 0.6f;
            float haloWidth = 0.1f;
            float chromaticOffset = 0.01f;
            std::uint32_t ghostCount = 4;
            /** @brief The view's width over its height. */
            float aspect = 1.0f;
            Math::Vec2 pad{0.0f};
        };
        static_assert(sizeof(FlareBlock) == sizeof(BloomBlock));

        template <typename Block>
        std::span<const std::byte> BytesOf(const Block& block)
        {
            return std::as_bytes(std::span(&block, 1));
        }

        /** @brief Level `level`'s size: the view's halved `level + 1` times, at least one pixel. */
        PixelSize LevelSize(PixelSize view, std::uint32_t level)
        {
            return {std::max(view.width >> (level + 1), 1u), std::max(view.height >> (level + 1), 1u)};
        }

        /** @brief Streak level `level`'s size: level 0's width halved `level + 1` times, at least one pixel; its
         * height.
         */
        PixelSize StreakLevelSize(PixelSize view, std::uint32_t level)
        {
            const PixelSize level0 = LevelSize(view, 0);
            return {std::max(level0.width >> (level + 1), 1u), level0.height};
        }

        BloomBlock BlockFor(PixelSize source, float blend, bool karis, Math::Vec3 tint = Math::Vec3(1.0f),
                            float dirtIntensity = 0.0f)
        {
            BloomBlock block;
            block.tint = tint;
            block.dirtIntensity = dirtIntensity;
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

    std::uint32_t StreakLevels(PixelSize size) noexcept
    {
        if (BloomLevels(size) == 0)
        {
            return 0;
        }
        std::uint32_t levels = 0;
        for (std::uint32_t side = LevelSize(size, 0).width; side > 1 && levels < MaxStreakLevels; side /= 2)
        {
            ++levels;
        }
        return levels;
    }

    std::vector<float> StreakLevelWeights(float length, std::uint32_t levels)
    {
        return BloomLevelWeights(length, levels);
    }

    BloomChain::BloomChain(GraphicsDevice& device, ILogger& logger, std::uint32_t samples)
        : m_device(device),
          m_passes(device, logger, "Bloom", RenderStage::PostProcess, MirroredSampler(), sizeof(BloomBlock)),
          m_lensDirtPasses(device, logger, "Lens dirt", RenderStage::PostProcess, MirroredSampler(), sizeof(BloomBlock))
    {
        m_downsample = ReadBuiltin(BuiltinProgram::BloomDownsample, logger);
        m_upsample = ReadBuiltin(BuiltinProgram::BloomUpsample, logger);
        m_flare = ReadBuiltin(BuiltinProgram::LensFlare, logger);

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

        // The streaks' passes: the horizontal variants, the composite again at SceneColor's samples.
        constexpr std::array<std::string_view, 1> horizontal{"HORIZONTAL"};
        m_streakDownsample = m_passes.MakePipeline(m_downsample.FindVariant(horizontal), m_downsample.name,
                                                   {TextureFormat::Rgba16Float});
        const ShaderVariant* streakUpsample = m_upsample.FindVariant(horizontal);
        m_streakUpsample =
            m_passes.MakePipeline(streakUpsample, m_upsample.name, {TextureFormat::Rgba16Float}, BlendMode::Alpha);
        m_streakComposite = m_passes.MakePipeline(streakUpsample, m_upsample.name, {TextureFormat::Rgba16Float},
                                                  BlendMode::Alpha, samples);

        // The flare's ghosts, and the composite that also reads the lens dirt, at SceneColor's samples.
        m_flareGhosts = m_passes.MakePipeline(m_flare.FindVariant({}), m_flare.name, {TextureFormat::Rgba16Float});
        constexpr std::array<std::string_view, 1> dirt{"DIRT"};
        m_lensDirtComposite = m_lensDirtPasses.MakePipeline(m_upsample.FindVariant(dirt), m_upsample.name,
                                                            {TextureFormat::Rgba16Float}, BlendMode::Alpha, samples);
    }

    BloomChain::~BloomChain()
    {
        m_device.Destroy(m_firstDownsample);
        m_device.Destroy(m_downsamplePipeline);
        m_device.Destroy(m_upsamplePipeline);
        m_device.Destroy(m_compositePipeline);
        m_device.Destroy(m_streakDownsample);
        m_device.Destroy(m_streakUpsample);
        m_device.Destroy(m_streakComposite);
        m_device.Destroy(m_flareGhosts);
        m_device.Destroy(m_lensDirtComposite);
    }

    void BloomChain::AddPasses(RenderGraph& graph, std::uint64_t frameIndex, std::span<const BloomView> views)
    {
        if (m_firstDownsample.IsEmpty() || m_downsamplePipeline.IsEmpty() || m_upsamplePipeline.IsEmpty() ||
            m_compositePipeline.IsEmpty())
        {
            return;
        }
        const bool canStreak =
            !m_streakDownsample.IsEmpty() && !m_streakUpsample.IsEmpty() && !m_streakComposite.IsEmpty();

        const auto blooms = [](const BloomView& view) {
            return view.settings.intensity > 0.0f && BloomLevels(view.size) > 0;
        };
        const auto streaks = [canStreak](const BloomView& view) {
            return canStreak && view.streaks.intensity > 0.0f && StreakLevels(view.size) > 0;
        };
        const auto flares = [this](const BloomView& view) {
            return !m_flareGhosts.IsEmpty() && view.flare.intensity > 0.0f && BloomLevels(view.size) > 0;
        };
        const auto hasLensDirt = [this](const BloomView& view) {
            return !m_lensDirtComposite.IsEmpty() && !view.dirt.IsEmpty() && view.flare.dirtIntensity > 0.0f;
        };
        // How many of bloom's levels a view downsamples to: all of them when it blooms, else down to the flare's level
        // when it flares, else level 0 for the streaks.
        const auto downsampled = [&](const BloomView& view) {
            return blooms(view) ? BloomLevels(view.size) : flares(view) ? FlareLevel(view.size) + 1 : 1;
        };
        // Each view's passes take one block apiece: a downsample per level and, when it blooms, an upsample per level
        // but the last and its composite; the streaks' downsample per level, as many upsamples less one and their
        // composite; and the flare's three. The composites under lens dirt take theirs from the lens dirt ring.
        std::size_t slots = 0;
        std::size_t lensDirtSlots = 0;
        for (const BloomView& view : views)
        {
            const bool bloom = blooms(view);
            const bool flare = flares(view);
            if (!bloom && !streaks(view) && !flare)
            {
                continue;
            }
            const std::uint32_t levels = downsampled(view);
            slots += levels + (bloom ? levels - 1 : 0);
            slots += streaks(view) ? std::size_t{2} * StreakLevels(view.size) : 0;
            slots += flare ? 2 : 0;
            const std::size_t composites = (bloom ? 1 : 0) + (flare ? 1 : 0);
            (hasLensDirt(view) ? lensDirtSlots : slots) += composites;
        }
        if (slots == 0)
        {
            return;
        }
        m_passes.Reserve(slots);
        m_lensDirtPasses.Reserve(lensDirtSlots);

        const auto addPass = [&](const char* name, PipelineHandle pipeline, GraphResource source, GraphResource target,
                                 LoadOp load, PixelSize sourceSize, PixelSize targetSize, float blend, bool karis,
                                 Math::Vec3 tint = Math::Vec3(1.0f)) {
            const BindingGroupHandle block =
                m_passes.Block(frameIndex, BytesOf(BlockFor(sourceSize, blend, karis, tint)));
            m_passes.AddPass(graph, name, pipeline, block, {source}, {target}, load,
                             {0, 0, targetSize.width, targetSize.height});
        };
        // A composite into SceneColor: under lens dirt, the DIRT variant reading the dirt too.
        const auto addComposite = [&](const char* name, const BloomView& view, GraphResource source,
                                      PixelSize sourceSize, float intensity) {
            const PixelRect viewport{0, 0, view.size.width, view.size.height};
            if (hasLensDirt(view))
            {
                const BindingGroupHandle block = m_lensDirtPasses.Block(
                    frameIndex,
                    BytesOf(BlockFor(sourceSize, intensity, false, Math::Vec3(1.0f), view.flare.dirtIntensity)));
                m_lensDirtPasses.AddPass(graph, name, m_lensDirtComposite, block, {source, view.dirt},
                                         {view.sceneColour}, LoadOp::Load, viewport);
                return;
            }
            const BindingGroupHandle block =
                m_passes.Block(frameIndex, BytesOf(BlockFor(sourceSize, intensity, false)));
            m_passes.AddPass(graph, name, m_compositePipeline, block, {source}, {view.sceneColour}, LoadOp::Load,
                             viewport);
        };
        const auto makeLevel = [&](const char* name, PixelSize size) {
            TextureDesc desc;
            desc.format = TextureFormat::Rgba16Float;
            desc.width = size.width;
            desc.height = size.height;
            desc.usage = TextureUsage::ColourAttachment | TextureUsage::Sampled;
            return graph.CreateTexture(name, desc);
        };

        for (const BloomView& view : views)
        {
            const bool bloom = blooms(view);
            const bool streak = streaks(view);
            const bool flare = flares(view);
            if (!bloom && !streak && !flare)
            {
                continue;
            }

            // Bloom's downsamples, or only those the streaks and the flare read when the view does not bloom: each
            // covers its whole level, so nothing is loaded.
            const std::uint32_t levels = downsampled(view);
            std::vector<GraphResource> chain;
            for (std::uint32_t level = 0; level < levels; ++level)
            {
                chain.push_back(makeLevel("Bloom", LevelSize(view.size, level)));
            }
            for (std::uint32_t level = 0; level < levels; ++level)
            {
                const bool first = level == 0;
                addPass("BloomDownsample", first ? m_firstDownsample : m_downsamplePipeline,
                        first ? view.sceneColour : chain[level - 1], chain[level], LoadOp::DontCare,
                        first ? view.size : LevelSize(view.size, level - 1), LevelSize(view.size, level), 1.0f, first);
            }

            // The streaks' downsamples read level 0 before bloom's upsamples blend the wider levels over it.
            const std::uint32_t streakLevels = streak ? StreakLevels(view.size) : 0;
            std::vector<GraphResource> streakChain;
            for (std::uint32_t level = 0; level < streakLevels; ++level)
            {
                streakChain.push_back(makeLevel("Streak", StreakLevelSize(view.size, level)));
            }
            for (std::uint32_t level = 0; level < streakLevels; ++level)
            {
                const bool first = level == 0;
                addPass("StreakDownsample", m_streakDownsample, first ? chain[0] : streakChain[level - 1],
                        streakChain[level], LoadOp::DontCare,
                        first ? LevelSize(view.size, 0) : StreakLevelSize(view.size, level - 1),
                        StreakLevelSize(view.size, level), 1.0f, false);
            }

            // The flare's ghosts and halo read level 1 before bloom's upsamples blend the wider levels over it, so the
            // flare is the same with bloom on or off and at any bloom radius; then their blur.
            GraphResource flareLevel;
            const PixelSize flareSize = LevelSize(view.size, FlareLevel(view.size));
            if (flare)
            {
                FlareBlock block;
                block.ghostSpacing = view.flare.ghostSpacing;
                block.haloRadius = view.flare.haloRadius;
                block.haloWidth = view.flare.haloWidth;
                block.chromaticOffset = view.flare.chromaticOffset;
                block.ghostCount = std::min(view.flare.ghostCount, MaxGhosts);
                block.aspect = static_cast<float>(view.size.width) / static_cast<float>(view.size.height);
                const GraphResource ghosts = makeLevel("Flare", flareSize);
                m_passes.AddPass(graph, "FlareGhosts", m_flareGhosts, m_passes.Block(frameIndex, BytesOf(block)),
                                 {chain[FlareLevel(view.size)]}, {ghosts}, LoadOp::DontCare,
                                 {0, 0, flareSize.width, flareSize.height});
                flareLevel = makeLevel("Flare", flareSize);
                addPass("FlareBlur", m_downsamplePipeline, ghosts, flareLevel, LoadOp::DontCare, flareSize, flareSize,
                        1.0f, false);
            }

            if (bloom)
            {
                const float intensity = std::min(view.settings.intensity, 1.0f);
                const float radius = std::clamp(view.settings.radius, 0.0f, 1.0f);
                for (std::uint32_t level = levels - 1; level-- > 0;)
                {
                    addPass("BloomUpsample", m_upsamplePipeline, chain[level + 1], chain[level], LoadOp::Load,
                            LevelSize(view.size, level + 1), LevelSize(view.size, level), radius, false);
                }
                addComposite("BloomComposite", view, chain[0], LevelSize(view.size, 0), intensity);
            }

            if (streak)
            {
                const float intensity = std::min(view.streaks.intensity, 1.0f);
                const float length = std::clamp(view.streaks.length, 0.0f, 1.0f);
                for (std::uint32_t level = streakLevels - 1; level-- > 0;)
                {
                    addPass("StreakUpsample", m_streakUpsample, streakChain[level + 1], streakChain[level],
                            LoadOp::Load, StreakLevelSize(view.size, level + 1), StreakLevelSize(view.size, level),
                            length, false);
                }
                addPass("StreakComposite", m_streakComposite, streakChain[0], view.sceneColour, LoadOp::Load,
                        StreakLevelSize(view.size, 0), view.size, intensity, false,
                        UnitLuminanceTint(view.streaks.tint));
            }

            if (flare)
            {
                addComposite("FlareComposite", view, flareLevel, flareSize, std::min(view.flare.intensity, 1.0f));
            }
        }
    }
} // namespace Fenrir::Render
