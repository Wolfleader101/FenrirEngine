#include "Core/Gtao.hpp"

#include <algorithm>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/BuiltinShaders.hpp"
#include "Core/FullscreenPipeline.hpp"
#include "FenrirLogger/ILogger.hpp"

namespace Fenrir::Render
{
    namespace
    {
        using namespace Fenrir::Graphics;

        /** @brief A pass's block, as `builtin:gtao` declares it (std140). */
        struct GtaoBlock
        {
            Math::Mat4 projection{1.0f};
            Math::Mat4 inverseProjection{1.0f};
            /** @brief The view's size in pixels, and one over it. */
            Math::Vec4 size{1.0f};
            /** @brief x: the effect radius; y: the final power; z: the denoise pass's blur beta. */
            Math::Vec4 settings{0.0f};
        };
        static_assert(sizeof(GtaoBlock) == 160);

        // Nearest and clamped: every read is a texelFetch.
        SamplerDesc NearestSampler()
        {
            SamplerDesc sampler;
            sampler.minFilter = Filter::Nearest;
            sampler.magFilter = Filter::Nearest;
            sampler.mipFilter = Filter::Nearest;
            sampler.addressU = AddressMode::ClampToEdge;
            sampler.addressV = AddressMode::ClampToEdge;
            sampler.addressW = AddressMode::ClampToEdge;
            return sampler;
        }
    } // namespace

    bool HasAmbientOcclusion(const AmbientOcclusionSettings& settings) noexcept
    {
        return settings.enabled && settings.radius > 0.0f && settings.intensity > 0.0f;
    }

    Gtao::Gtao(GraphicsDevice& device, ILogger& logger, std::uint32_t samples)
        : m_device(device),
          m_passes(device, logger, "GTAO", RenderStage::DepthPrepass, NearestSampler(), sizeof(GtaoBlock))
    {
        m_program = ReadBuiltin(BuiltinProgram::Gtao, logger);
        // Under MSAA the horizon search reads sample 0 of the multisampled prepass targets.
        constexpr std::array<std::string_view, 1> msaa{"MSAA"};
        constexpr std::array<std::string_view, 1> denoise{"DENOISE"};
        m_horizons = m_passes.MakePipeline(samples > 1 ? m_program.FindVariant(msaa) : m_program.FindVariant({}),
                                           m_program.name, {TextureFormat::R8Unorm, TextureFormat::R8Unorm});
        m_denoise = m_passes.MakePipeline(m_program.FindVariant(denoise), m_program.name, {TextureFormat::R8Unorm});
    }

    Gtao::~Gtao()
    {
        m_device.Destroy(m_horizons);
        m_device.Destroy(m_denoise);
    }

    void Gtao::BeginFrame(std::size_t views)
    {
        m_passes.Reserve(views * 2);
    }

    GraphResource Gtao::AddPasses(RenderGraph& graph, std::uint64_t frameIndex, const GtaoView& view)
    {
        if (!HasAmbientOcclusion(view.settings) || m_horizons.IsEmpty() || m_denoise.IsEmpty() ||
            view.size.width == 0 || view.size.height == 0)
        {
            return {};
        }
        GtaoBlock block;
        block.projection = view.projection;
        block.inverseProjection = glm::inverse(view.projection);
        const auto width = static_cast<float>(view.size.width);
        const auto height = static_cast<float>(view.size.height);
        block.size = Math::Vec4(width, height, 1.0f / width, 1.0f / height);
        block.settings = Math::Vec4(view.settings.radius * GtaoRadiusMultiplier,
                                    GtaoFinalPower * view.settings.intensity, GtaoBlurBeta / 5.0f, 0.0f);
        // The horizon search and first denoise pass read one block, the final denoise pass the next.
        const BindingGroupHandle firstBlock = m_passes.Block(frameIndex, std::as_bytes(std::span(&block, 1)));
        block.settings.z = GtaoBlurBeta;
        const BindingGroupHandle finalBlock = m_passes.Block(frameIndex, std::as_bytes(std::span(&block, 1)));

        TextureDesc desc;
        desc.format = TextureFormat::R8Unorm;
        desc.width = view.size.width;
        desc.height = view.size.height;
        desc.usage = TextureUsage::ColourAttachment | TextureUsage::Sampled;
        const GraphResource raw = graph.CreateTexture("GtaoOcclusion", desc);
        const GraphResource edges = graph.CreateTexture("GtaoEdges", desc);
        const GraphResource blurred = graph.CreateTexture("GtaoBlurred", desc);
        const GraphResource occlusion = graph.CreateTexture("AmbientOcclusion", desc);
        const PixelRect viewport{0, 0, view.size.width, view.size.height};

        const auto addPass = [&](const char* name, PipelineHandle pipeline, BindingGroupHandle blockGroup,
                                 GraphResource first, GraphResource second, std::vector<GraphResource> targets) {
            // Each pass covers its targets whole, so nothing is loaded.
            m_passes.AddPass(graph, name, pipeline, blockGroup, {first, second}, std::move(targets), LoadOp::DontCare,
                             viewport);
        };
        addPass("GtaoHorizons", m_horizons, firstBlock, view.sceneDepth, view.sceneNormals, {raw, edges});
        addPass("GtaoDenoise", m_denoise, firstBlock, raw, edges, {blurred});
        addPass("GtaoDenoise", m_denoise, finalBlock, blurred, edges, {occlusion});
        return occlusion;
    }
} // namespace Fenrir::Render
