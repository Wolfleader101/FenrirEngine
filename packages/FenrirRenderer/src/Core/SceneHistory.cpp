#include "Core/SceneHistory.hpp"

#include <algorithm>
#include <string_view>

#include "Core/BuiltinShaders.hpp"
#include "Core/ImageUpload.hpp"
#include "FenrirLogger/ILogger.hpp"

namespace Fenrir::Render
{
    namespace
    {
        using namespace Fenrir::Graphics;

        /** @brief A pass's block, as `builtin:hi-z` and `builtin:scene-history` declare it (std140). */
        struct LevelBlock
        {
            /** @brief The source's width and height in texels, then the level drawn's. */
            std::array<std::uint32_t, 4> size{};
        };
        static_assert(sizeof(LevelBlock) == 16);

        TextureDesc ChainDesc(TextureFormat format, PixelSize size)
        {
            TextureDesc desc;
            desc.format = format;
            desc.width = size.width;
            desc.height = size.height;
            desc.mipLevels = MipCount(size.width, size.height);
            desc.usage = TextureUsage::ColourAttachment | TextureUsage::Sampled;
            return desc;
        }
    } // namespace

    PixelSize MipSize(PixelSize size, std::uint32_t level) noexcept
    {
        const auto halved = [level](std::uint32_t side) { return level >= 32 ? 1u : std::max(side >> level, 1u); };
        return {halved(size.width), halved(size.height)};
    }

    std::vector<HiZTexel> ReduceHiZ(std::span<const HiZTexel> level, PixelSize size)
    {
        const PixelSize next = MipSize(size, 1);
        std::vector<HiZTexel> reduced(std::size_t{next.width} * next.height);
        // As builtin:hi-z: two texels a side, three for the last texel on a side whose source is odd.
        const auto extent = [](std::uint32_t at, std::uint32_t target, std::uint32_t source) {
            return 2u + (at + 1 == target ? source & 1u : 0u);
        };
        for (std::uint32_t y = 0; y < next.height; ++y)
        {
            for (std::uint32_t x = 0; x < next.width; ++x)
            {
                HiZTexel texel{0.0f, 1.0f};
                for (std::uint32_t dy = 0; dy < extent(y, next.height, size.height); ++dy)
                {
                    for (std::uint32_t dx = 0; dx < extent(x, next.width, size.width); ++dx)
                    {
                        const std::uint32_t sx = std::min(2 * x + dx, size.width - 1);
                        const std::uint32_t sy = std::min(2 * y + dy, size.height - 1);
                        const HiZTexel& source = level[std::size_t{sy} * size.width + sx];
                        texel.nearest = std::max(texel.nearest, source.nearest);
                        texel.farthest = std::min(texel.farthest, source.farthest);
                    }
                }
                reduced[std::size_t{y} * next.width + x] = texel;
            }
        }
        return reduced;
    }

    SceneHistory::SceneHistory(GraphicsDevice& device, ILogger& logger, std::uint32_t samples)
        : m_device(device), m_samples(std::max(samples, 1u)),
          m_hiZPasses(device, logger, "HiZ", RenderStage::DepthPrepass, NearestClampedSampler(), sizeof(LevelBlock)),
          m_historyPasses(device, logger, "SceneHistory", RenderStage::PostProcess, NearestClampedSampler(),
                          sizeof(LevelBlock))
    {
        m_hiZProgram = ReadBuiltin(BuiltinProgram::HiZ, logger);
        m_historyProgram = ReadBuiltin(BuiltinProgram::SceneHistory, logger);
        // Under MSAA the copies read the multisampled scene targets: every sample of depth and of colour.
        const bool msaa = m_samples > 1;
        constexpr std::array<std::string_view, 1> copy{"COPY"};
        constexpr std::array<std::string_view, 2> copyMsaa{"COPY", "MSAA"};
        constexpr std::array<std::string_view, 1> msaaOnly{"MSAA"};
        constexpr std::array<std::string_view, 1> downsample{"DOWNSAMPLE"};
        m_hiZCopy = m_hiZPasses.MakePipeline(msaa ? m_hiZProgram.FindVariant(copyMsaa) : m_hiZProgram.FindVariant(copy),
                                             m_hiZProgram.name, {TextureFormat::Rg32Float});
        m_hiZReduce =
            m_hiZPasses.MakePipeline(m_hiZProgram.FindVariant({}), m_hiZProgram.name, {TextureFormat::Rg32Float});
        m_historyCopy = m_historyPasses.MakePipeline(msaa ? m_historyProgram.FindVariant(msaaOnly)
                                                          : m_historyProgram.FindVariant({}),
                                                     m_historyProgram.name, {TextureFormat::Rgba16Float});
        m_historyDownsample = m_historyPasses.MakePipeline(m_historyProgram.FindVariant(downsample),
                                                           m_historyProgram.name, {TextureFormat::Rgba16Float});
    }

    SceneHistory::~SceneHistory()
    {
        for (auto& [camera, entry] : m_entries)
        {
            Destroy(entry);
        }
        m_device.Destroy(m_hiZCopy);
        m_device.Destroy(m_hiZReduce);
        m_device.Destroy(m_historyCopy);
        m_device.Destroy(m_historyDownsample);
    }

    void SceneHistory::BeginFrame(std::span<const ViewRequest> views, bool occlusion)
    {
        // Each pass draws one level, with its own block of sizes: a pyramid is built twice when occlusion culling
        // may want the first build and reflections the second.
        std::size_t pyramidLevels = 0;
        std::size_t historyLevels = 0;
        for (const ViewRequest& view : views)
        {
            const bool reflections = HasReflections(view.reflections);
            const std::uint32_t levels = MipCount(view.viewport.width, view.viewport.height);
            pyramidLevels += (reflections || occlusion) ? levels * (occlusion ? 2 : 1) : 0;
            historyLevels += reflections ? levels : 0;
        }
        m_hiZPasses.Reserve(pyramidLevels);
        m_historyPasses.Reserve(historyLevels);
        for (auto& [camera, entry] : m_entries)
        {
            entry.seen = false;
        }
    }

    HistoryView SceneHistory::Import(RenderGraph& graph, const ViewRequest& view, PixelSize size,
                                     const Math::Mat4& viewProjection, bool occlusion)
    {
        const bool reflections = HasReflections(view.reflections);
        if ((!reflections && !occlusion) || size.width == 0 || size.height == 0 || m_hiZCopy.IsEmpty() ||
            m_hiZReduce.IsEmpty() || m_historyCopy.IsEmpty() || m_historyDownsample.IsEmpty())
        {
            return {};
        }
        Entry& entry = m_entries[view.camera];
        if (entry.seen)
        {
            return {};
        }
        entry.seen = true;

        // Made, or remade at a new size or samples, or when the camera starts or stops reflecting, the textures hold
        // nothing of last frame. A camera that only culls by occlusion keeps one pyramid and no history.
        const TextureDesc historyDesc = ChainDesc(TextureFormat::Rgba16Float, size);
        const TextureDesc pyramidDesc = ChainDesc(TextureFormat::Rg32Float, size);
        const bool fresh =
            entry.pyramids[0].IsEmpty() || entry.pyramidDesc != pyramidDesc || entry.history.IsEmpty() == reflections;
        if (fresh)
        {
            Destroy(entry);
            entry.pyramids[0] = m_device.CreateTexture(pyramidDesc, "Hi-Z pyramid");
            if (reflections)
            {
                entry.history = m_device.CreateTexture(historyDesc, "Scene history");
                entry.pyramids[1] = m_device.CreateTexture(pyramidDesc, "Hi-Z pyramid");
            }
            entry.historyDesc = historyDesc;
            entry.pyramidDesc = pyramidDesc;
            entry.current = 0;
            entry.viewProjection = viewProjection;
        }
        else if (reflections)
        {
            entry.current = 1 - entry.current;
        }

        HistoryView frame;
        frame.pyramid = graph.ImportTexture("HiZ", entry.pyramids.at(entry.current), pyramidDesc);
        frame.pyramidTexture = entry.pyramids.at(entry.current);
        if (reflections)
        {
            frame.history = graph.ImportTexture("SceneHistory", entry.history, historyDesc);
            frame.previousPyramid =
                graph.ImportTexture("PreviousHiZ", entry.pyramids.at(1 - entry.current), pyramidDesc);
        }
        frame.size = size;
        frame.mips = pyramidDesc.mipLevels;
        frame.previousViewProjection = entry.viewProjection;
        frame.valid = !fresh;
        entry.viewProjection = viewProjection;
        return frame;
    }

    void SceneHistory::AddHiZPasses(RenderGraph& graph, std::uint64_t frameIndex, const HistoryView& view,
                                    GraphResource sceneDepth)
    {
        if (!view.IsEmpty())
        {
            AddChain(m_hiZPasses, graph, frameIndex, "HiZ", m_hiZCopy, m_hiZReduce, sceneDepth, view.pyramid, view);
        }
    }

    void SceneHistory::AddHistoryPasses(RenderGraph& graph, std::uint64_t frameIndex, const HistoryView& view,
                                        GraphResource sceneColour)
    {
        if (view.HasReflections())
        {
            AddChain(m_historyPasses, graph, frameIndex, "SceneHistory", m_historyCopy, m_historyDownsample,
                     sceneColour, view.history, view);
        }
    }

    void SceneHistory::EndFrame()
    {
        for (auto it = m_entries.begin(); it != m_entries.end();)
        {
            if (!it->second.seen)
            {
                Destroy(it->second);
                it = m_entries.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void SceneHistory::Destroy(Entry& entry)
    {
        m_device.Destroy(entry.history);
        for (TextureHandle& pyramid : entry.pyramids)
        {
            m_device.Destroy(pyramid);
            pyramid = {};
        }
        entry.history = {};
    }

    void SceneHistory::AddChain(FullscreenPasses<1>& passes, RenderGraph& graph, std::uint64_t frameIndex,
                                const char* name, PipelineHandle first, PipelineHandle down, GraphResource source,
                                GraphResource target, const HistoryView& view)
    {
        for (std::uint32_t level = 0; level < view.mips; ++level)
        {
            const PixelSize from = level == 0 ? view.size : MipSize(view.size, level - 1);
            const PixelSize to = MipSize(view.size, level);
            LevelBlock block;
            block.size = {from.width, from.height, to.width, to.height};
            const BindingGroupHandle group = passes.Block(frameIndex, std::as_bytes(std::span(&block, 1)));
            if (level == 0)
            {
                passes.AddMipPass(graph, name, first, group, {source}, std::nullopt, target, 0, to);
            }
            else
            {
                passes.AddMipPass(graph, name, down, group, {target}, level - 1, target, level, to);
            }
        }
    }
} // namespace Fenrir::Render
