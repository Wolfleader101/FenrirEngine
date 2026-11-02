#include "Core/Gtao.hpp"

#include <algorithm>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/BuiltinShaders.hpp"
#include "FenrirLogger/ILogger.hpp"

namespace Fenrir::Render
{
    namespace
    {
        using namespace Fenrir::Graphics;

        /** @brief The binding sets of the passes' block and source textures. */
        constexpr std::uint32_t BlockSet = 1;
        constexpr std::uint32_t SourceSet = 2;

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
    } // namespace

    bool HasAmbientOcclusion(const AmbientOcclusionSettings& settings) noexcept
    {
        return settings.enabled && settings.radius > 0.0f && settings.intensity > 0.0f;
    }

    Gtao::Gtao(GraphicsDevice& device, ILogger& logger, std::uint32_t samples) : m_device(device), m_logger(logger)
    {
        ShaderContainerResult read = ReadShaderContainer(GetBuiltinShader(BuiltinProgram::Gtao).bytes);
        if (read.Ok())
        {
            m_program = std::move(*read.container);
        }
        else
        {
            m_logger.Error("FenrirRenderer: the built-in shader '{}' does not read: {}",
                           GetBuiltinShader(BuiltinProgram::Gtao).name, read.diagnostic);
        }
        m_blockLayout = m_device.CreateBindingGroupLayout({{{0, ShaderBindingKind::UniformBuffer}}}, "GTAO block");
        m_sourceLayout = m_device.CreateBindingGroupLayout(
            {{{0, ShaderBindingKind::Texture}, {1, ShaderBindingKind::Texture}}}, "GTAO sources");

        // Under MSAA the horizon search reads sample 0 of the multisampled prepass targets.
        constexpr std::array<std::string_view, 1> msaa{"MSAA"};
        constexpr std::array<std::string_view, 1> denoise{"DENOISE"};
        m_horizons = MakePipeline(samples > 1 ? m_program.FindVariant(msaa) : m_program.FindVariant({}), 2);
        m_denoise = MakePipeline(m_program.FindVariant(denoise), 1);

        SamplerDesc sampler;
        sampler.minFilter = Filter::Nearest;
        sampler.magFilter = Filter::Nearest;
        sampler.mipFilter = Filter::Nearest;
        sampler.addressU = AddressMode::ClampToEdge;
        sampler.addressV = AddressMode::ClampToEdge;
        sampler.addressW = AddressMode::ClampToEdge;
        m_sampler = m_device.CreateSampler(sampler, "GTAO sampler");
        m_blocks = std::make_unique<BlockRing>(m_device, m_blockLayout, false, sizeof(GtaoBlock), "GTAO");
    }

    Gtao::~Gtao()
    {
        for (const auto& [key, group] : m_sourceGroups)
        {
            m_device.Destroy(group);
        }
        m_blocks.reset();
        m_device.Destroy(m_sampler);
        m_device.Destroy(m_horizons);
        m_device.Destroy(m_denoise);
        m_device.Destroy(m_sourceLayout);
        m_device.Destroy(m_blockLayout);
    }

    PipelineHandle Gtao::MakePipeline(const ShaderVariant* variant, std::size_t outputs)
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
        desc.colourFormats.assign(outputs, TextureFormat::R8Unorm);
        desc.depth.test = false;
        desc.depth.write = false;
        desc.depthFormat.reset();
        const PipelineResult made = m_device.CreatePipeline(desc, m_program.name);
        if (made.pipeline.IsEmpty())
        {
            m_logger.Error("FenrirRenderer: the built-in pipeline '{}' could not be made: {}", m_program.name,
                           made.diagnostic);
        }
        return made.pipeline;
    }

    BindingGroupHandle Gtao::SourceGroup(TextureHandle first, TextureHandle second)
    {
        const std::array<std::uint32_t, 4> key{first.index, first.generation, second.index, second.generation};
        if (const auto found = m_sourceGroups.find(key); found != m_sourceGroups.end())
        {
            return found->second;
        }
        std::vector<BindingGroupEntry> entries(2);
        entries[0].binding = 0;
        entries[0].texture = first;
        entries[0].sampler = m_sampler;
        entries[1].binding = 1;
        entries[1].texture = second;
        entries[1].sampler = m_sampler;
        const BindingGroupHandle group = m_device.CreateBindingGroup({m_sourceLayout, entries}, "GTAO sources");
        m_sourceGroups.emplace(key, group);
        return group;
    }

    void Gtao::BeginFrame(std::size_t views)
    {
        // A transient texture's group outlives its frame, so groups of textures the pool has since freed go here.
        for (auto it = m_sourceGroups.begin(); it != m_sourceGroups.end();)
        {
            const std::array<std::uint32_t, 4>& key = it->first;
            if (!m_device.IsLive(TextureHandle{key[0], key[1]}) || !m_device.IsLive(TextureHandle{key[2], key[3]}))
            {
                m_device.Destroy(it->second);
                it = m_sourceGroups.erase(it);
            }
            else
            {
                ++it;
            }
        }
        m_blocks->Reserve(views * 2);
    }

    GraphResource Gtao::AddPasses(RenderGraph& graph, std::uint64_t frameIndex, std::size_t slot, const GtaoView& view)
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
        m_blocks->Write(frameIndex, slot * 2, std::as_bytes(std::span(&block, 1)));
        block.settings.z = GtaoBlurBeta;
        m_blocks->Write(frameIndex, slot * 2 + 1, std::as_bytes(std::span(&block, 1)));
        const BindingGroupHandle firstBlock = m_blocks->Group(frameIndex, slot * 2);
        const BindingGroupHandle finalBlock = m_blocks->Group(frameIndex, slot * 2 + 1);

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
            graph.AddPass(
                name, RenderStage::DepthPrepass, QueueKind::Graphics,
                [=](PassBuilder& pass) {
                    pass.Sample(first);
                    pass.Sample(second);
                    // Each pass covers its targets whole, so nothing is loaded.
                    for (const GraphResource target : targets)
                    {
                        pass.ColourAttachment(target, LoadOp::DontCare);
                    }
                },
                [this, pipeline, blockGroup, first, second, viewport](PassContext& context) {
                    PassEncoder& encoder = context.Encoder();
                    encoder.SetViewport(viewport);
                    encoder.SetPipeline(pipeline);
                    encoder.SetBindingGroup(BlockSet, blockGroup);
                    encoder.SetBindingGroup(SourceSet, SourceGroup(context.Texture(first), context.Texture(second)));
                    encoder.Draw({.vertexCount = 3});
                });
        };
        addPass("GtaoHorizons", m_horizons, firstBlock, view.sceneDepth, view.sceneNormals, {raw, edges});
        addPass("GtaoDenoise", m_denoise, firstBlock, raw, edges, {blurred});
        addPass("GtaoDenoise", m_denoise, finalBlock, blurred, edges, {occlusion});
        return occlusion;
    }
} // namespace Fenrir::Render
