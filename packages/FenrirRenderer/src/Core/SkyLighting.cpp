#include "Core/SkyLighting.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <span>

#include <glm/gtc/packing.hpp>

#include "Core/BuiltinShaders.hpp"
#include "Core/SphericalHarmonics.hpp"
#include "FenrirLogger/ILogger.hpp"

namespace Fenrir::Render
{
    namespace
    {
        using namespace Fenrir::Graphics;

        /** @brief The binding set of the passes' own blocks and textures, as of the material passes. */
        constexpr std::uint32_t PassSet = 2;
        /** @brief The lookup table's side. */
        constexpr std::uint32_t BrdfSide = 128;
        /** @brief The widest specular cube face, and the widest cube mip the projection reads. */
        constexpr std::uint32_t SpecularSide = 128;
        constexpr std::uint32_t ProjectionSide = 32;
        /** @brief The passes' blocks sit at multiples of this, the largest uniform offset alignment GL asks for. */
        constexpr std::size_t Slot = 256;

        /** @brief The SkyIrradiance pass's block, as `builtin:sky-irradiance` declares it (std140). */
        struct IrradianceBlock
        {
            std::uint32_t side = 1;
            float lod = 0.0f;
            std::uint32_t padding0 = 0;
            std::uint32_t padding1 = 0;
        };

        /** @brief A SkySpecular pass's block, as `builtin:sky-specular` declares it (std140). */
        struct SpecularBlock
        {
            std::uint32_t face = 0;
            float roughness = 0.0f;
            float sourceSide = 1.0f;
            float sourceMaxLod = 0.0f;
            float side = 1.0f;
            std::uint32_t padding0 = 0;
            std::uint32_t padding1 = 0;
            std::uint32_t padding2 = 0;
        };

        /** @brief How many mip levels a full chain for a face of `side` has, down to 1 x 1. */
        std::uint32_t MipCount(std::uint32_t side)
        {
            std::uint32_t levels = 1;
            for (; side > 1; side /= 2)
            {
                ++levels;
            }
            return levels;
        }

        /** @brief RGBA16F texels of `colours`, alpha 1. */
        std::vector<std::uint16_t> Halves(std::span<const Math::Vec3> colours)
        {
            std::vector<std::uint16_t> halves;
            for (const Math::Vec3 colour : colours)
            {
                for (const float channel : {colour.x, colour.y, colour.z, 1.0f})
                {
                    halves.push_back(glm::packHalf1x16(channel));
                }
            }
            return halves;
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

        /** @brief Set 2 of a pass program: its block at binding 0 and the sky cube at binding 1. */
        BindingGroupLayoutDesc PassLayout()
        {
            return {{{0, ShaderBindingKind::UniformBuffer}, {1, ShaderBindingKind::Texture}}};
        }
    } // namespace

    SkyLighting::SkyLighting(GraphicsDevice& device, ILogger& logger) : m_device(device), m_logger(logger)
    {
        m_irradianceProgram = ReadProgram(BuiltinProgram::SkyIrradiance, m_logger);
        m_specularProgram = ReadProgram(BuiltinProgram::SkySpecular, m_logger);
        m_brdfProgram = ReadProgram(BuiltinProgram::BrdfLut, m_logger);
        m_irradianceLayout = m_device.CreateBindingGroupLayout(PassLayout(), "Sky irradiance");
        m_specularLayout = m_device.CreateBindingGroupLayout(PassLayout(), "Sky specular");
        m_irradiancePipeline = MakePipeline(m_irradianceProgram, m_irradianceLayout, TextureFormat::Rgba16Float);
        m_specularPipeline = MakePipeline(m_specularProgram, m_specularLayout, TextureFormat::Rgba16Float);
        m_brdfPipeline = MakePipeline(m_brdfProgram, {}, TextureFormat::Rg16Float);

        SamplerDesc sampler;
        sampler.addressU = AddressMode::ClampToEdge;
        sampler.addressV = AddressMode::ClampToEdge;
        sampler.addressW = AddressMode::ClampToEdge;
        m_sampler = m_device.CreateSampler(sampler, "Sky lighting sampler");

        m_brdfDesc.format = TextureFormat::Rg16Float;
        m_brdfDesc.width = BrdfSide;
        m_brdfDesc.height = BrdfSide;
        m_brdfDesc.usage = TextureUsage::Sampled | TextureUsage::ColourAttachment;
        m_brdfLut = m_device.CreateTexture(m_brdfDesc, "BRDF lookup table");

        // Black until the first frame says what the sky is.
        Filter({});
    }

    SkyLighting::~SkyLighting()
    {
        Destroy(m_current);
        m_device.Destroy(m_brdfLut);
        m_device.Destroy(m_sampler);
        m_device.Destroy(m_irradiancePipeline);
        m_device.Destroy(m_specularPipeline);
        m_device.Destroy(m_brdfPipeline);
        m_device.Destroy(m_irradianceLayout);
        m_device.Destroy(m_specularLayout);
    }

    PipelineHandle SkyLighting::MakePipeline(const ShaderContainer& container, BindingGroupLayoutHandle layout,
                                             TextureFormat format)
    {
        PipelineDesc desc;
        desc.shader = container.FindVariant({});
        desc.sets.at(PassSet) = layout;
        desc.cull = CullMode::None;
        desc.colourFormats = {format};
        desc.depth.test = false;
        desc.depth.write = false;
        desc.depthFormat.reset();
        const PipelineResult made = m_device.CreatePipeline(desc, container.name);
        if (made.pipeline.IsEmpty())
        {
            m_logger.Error("FenrirRenderer: the built-in pipeline '{}' could not be made: {}", container.name,
                           made.diagnostic);
        }
        return made.pipeline;
    }

    void SkyLighting::Destroy(Filtered& filtered)
    {
        for (const BindingGroupHandle group : filtered.groups)
        {
            m_device.Destroy(group);
        }
        m_device.Destroy(filtered.params);
        m_device.Destroy(filtered.textures.irradiance);
        m_device.Destroy(filtered.textures.specular);
        filtered = {};
    }

    void SkyLighting::Filter(const SkySource& sky)
    {
        Filtered& filtered = m_current;
        filtered.source = sky;
        filtered.textures.brdfLut = m_brdfLut;

        TextureDesc irradiance;
        irradiance.format = TextureFormat::Rgba16Float;
        irradiance.width = 9;
        irradiance.height = 1;
        TextureDesc& specular = filtered.specularDesc;
        specular.kind = TextureKind::Cube;
        specular.format = TextureFormat::Rgba16Float;

        // A colour, or a cube whose passes cannot run: its coefficients and a 1 x 1 cube of it, written at once.
        if (sky.cube.IsEmpty() || m_irradiancePipeline.IsEmpty() || m_specularPipeline.IsEmpty())
        {
            irradiance.usage = TextureUsage::Sampled | TextureUsage::CopyDestination;
            specular.usage = TextureUsage::Sampled | TextureUsage::CopyDestination;
            filtered.textures.irradiance = m_device.CreateTexture(irradiance, "Sky irradiance");
            filtered.textures.specular = m_device.CreateTexture(specular, "Sky specular");
            const SkyIrradiance coefficients = ConstantIrradiance(sky.colour);
            const std::vector<std::uint16_t> texels = Halves(coefficients);
            m_device.WriteTexture(filtered.textures.irradiance, 0, 0, std::as_bytes(std::span(texels)));
            const std::vector<std::uint16_t> texel = Halves(std::span(&sky.colour, 1));
            for (std::uint32_t face = 0; face < 6; ++face)
            {
                m_device.WriteTexture(filtered.textures.specular, 0, face, std::as_bytes(std::span(texel)));
            }
            filtered.ready = true;
            return;
        }

        irradiance.usage = TextureUsage::Sampled | TextureUsage::ColourAttachment;
        specular.width = std::clamp(sky.cubeDesc.width, 1u, SpecularSide);
        specular.height = specular.width;
        specular.mipLevels = MipCount(specular.width);
        specular.usage = TextureUsage::Sampled | TextureUsage::ColourAttachment;
        filtered.textures.irradiance = m_device.CreateTexture(irradiance, "Sky irradiance");
        filtered.textures.specular = m_device.CreateTexture(specular, "Sky specular");
        filtered.textures.specularMaxLevel = static_cast<float>(specular.mipLevels - 1);

        // The projection reads the cube's first mip no wider than ProjectionSide.
        const std::uint32_t sourceLevels = std::max(sky.cubeDesc.mipLevels, 1u);
        std::uint32_t lod = 0;
        while (lod + 1 < sourceLevels && (sky.cubeDesc.width >> lod) > ProjectionSide)
        {
            ++lod;
        }
        std::vector<std::byte> blocks(Slot * (1 + std::size_t{6} * specular.mipLevels));
        const IrradianceBlock irradianceBlock{std::max(sky.cubeDesc.width >> lod, 1u), static_cast<float>(lod)};
        std::memcpy(blocks.data(), &irradianceBlock, sizeof(irradianceBlock));
        for (std::uint32_t level = 0; level < specular.mipLevels; ++level)
        {
            for (std::uint32_t face = 0; face < 6; ++face)
            {
                SpecularBlock block;
                block.face = face;
                block.roughness = specular.mipLevels > 1 ? static_cast<float>(level) / (specular.mipLevels - 1) : 0.0f;
                block.sourceSide = static_cast<float>(sky.cubeDesc.width);
                block.sourceMaxLod = static_cast<float>(sourceLevels - 1);
                block.side = static_cast<float>(std::max(specular.width >> level, 1u));
                std::memcpy(blocks.data() + Slot * (1 + std::size_t{level} * 6 + face), &block, sizeof(block));
            }
        }
        filtered.params = m_device.CreateBuffer({blocks.size(), BufferUsage::Uniform}, blocks, "Sky filtering");

        BindingGroupEntry cube;
        cube.binding = 1;
        cube.texture = sky.cube;
        cube.sampler = m_sampler;
        for (std::size_t pass = 0; pass < 1 + std::size_t{6} * specular.mipLevels; ++pass)
        {
            BindingGroupEntry params;
            params.binding = 0;
            params.buffer = filtered.params;
            params.offset = Slot * pass;
            params.size = pass == 0 ? sizeof(IrradianceBlock) : sizeof(SpecularBlock);
            filtered.groups.push_back(m_device.CreateBindingGroup(
                {pass == 0 ? m_irradianceLayout : m_specularLayout, {params, cube}}, "Sky filtering"));
        }
    }

    void SkyLighting::AddPasses(RenderGraph& graph, const SkySource& sky)
    {
        if (!m_brdfMade && !m_brdfPipeline.IsEmpty())
        {
            const GraphResource lut = graph.ImportTexture("BRDF lookup table", m_brdfLut, m_brdfDesc);
            graph.AddPass(
                "BrdfLut", RenderStage::Prepare, QueueKind::Graphics,
                [=](PassBuilder& pass) { pass.ColourAttachmentLevel(lut, 0); },
                [this](PassContext& context) {
                    PassEncoder& encoder = context.Encoder();
                    encoder.SetViewport({0, 0, BrdfSide, BrdfSide});
                    encoder.SetPipeline(m_brdfPipeline);
                    encoder.Draw({.vertexCount = 3});
                    m_brdfMade = true;
                });
        }

        const bool same = sky.cube.IsEmpty() ? m_current.source.cube.IsEmpty() && m_current.source.colour == sky.colour
                                             : m_current.source.cube == sky.cube;
        if (!same)
        {
            // The old textures go once the frames in flight are done with them.
            Destroy(m_current);
            Filter(sky);
        }
        if (!m_current.ready)
        {
            AddFilterPasses(graph);
        }
    }

    void SkyLighting::AddFilterPasses(RenderGraph& graph)
    {
        const SkySource& sky = m_current.source;
        const GraphResource source = graph.ImportTexture("Sky cube", sky.cube, sky.cubeDesc);
        TextureDesc irradianceDesc;
        irradianceDesc.format = TextureFormat::Rgba16Float;
        irradianceDesc.width = 9;
        irradianceDesc.height = 1;
        irradianceDesc.usage = TextureUsage::Sampled | TextureUsage::ColourAttachment;
        const GraphResource irradiance =
            graph.ImportTexture("Sky irradiance", m_current.textures.irradiance, irradianceDesc);
        const GraphResource specular =
            graph.ImportTexture("Sky specular", m_current.textures.specular, m_current.specularDesc);
        const TextureHandle current = m_current.textures.irradiance;

        // Each pass draws a whole level or face, so nothing is loaded; the Prepare stage runs them before any view.
        const BindingGroupHandle irradianceGroup = m_current.groups.at(0);
        graph.AddPass(
            "SkyIrradiance", RenderStage::Prepare, QueueKind::Graphics,
            [=](PassBuilder& pass) {
                pass.Sample(source);
                pass.ColourAttachmentLevel(irradiance, 0);
            },
            [this, irradianceGroup](PassContext& context) {
                PassEncoder& encoder = context.Encoder();
                encoder.SetViewport({0, 0, 9, 1});
                encoder.SetPipeline(m_irradiancePipeline);
                encoder.SetBindingGroup(PassSet, irradianceGroup);
                encoder.Draw({.vertexCount = 3});
            });
        const std::uint32_t levels = m_current.specularDesc.mipLevels;
        for (std::uint32_t level = 0; level < levels; ++level)
        {
            const std::uint32_t side = std::max(m_current.specularDesc.width >> level, 1u);
            for (std::uint32_t face = 0; face < 6; ++face)
            {
                const BindingGroupHandle group = m_current.groups.at(1 + std::size_t{level} * 6 + face);
                const bool last = level + 1 == levels && face == 5;
                graph.AddPass(
                    "SkySpecular", RenderStage::Prepare, QueueKind::Graphics,
                    [=](PassBuilder& pass) {
                        pass.Sample(source);
                        pass.ColourAttachmentLevel(specular, level, face);
                    },
                    [this, group, side, last, current](PassContext& context) {
                        PassEncoder& encoder = context.Encoder();
                        encoder.SetViewport({0, 0, side, side});
                        encoder.SetPipeline(m_specularPipeline);
                        encoder.SetBindingGroup(PassSet, group);
                        encoder.Draw({.vertexCount = 3});
                        // Recorded in this frame's graph, after every other filtering pass: nothing to add again.
                        if (last && m_current.textures.irradiance == current)
                        {
                            m_current.ready = true;
                        }
                    });
            }
        }
    }
} // namespace Fenrir::Render
