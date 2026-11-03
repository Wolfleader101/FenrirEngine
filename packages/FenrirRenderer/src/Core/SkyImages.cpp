#include "Core/SkyImages.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

#include "Core/BuiltinShaders.hpp"
#include "Core/FullscreenPasses.hpp"
#include "Core/ImageUpload.hpp"
#include "Core/UniformBlocks.hpp"

namespace Fenrir::Render
{
    namespace
    {
        using namespace Fenrir::Graphics;

        /** @brief The binding set of a Panorama pass's block and panorama. */
        constexpr std::uint32_t PanoramaSet = 2;

        /** @brief A Panorama pass's block, as `builtin:panorama` declares it (std140). */
        struct PanoramaBlock
        {
            std::uint32_t face = 0;
            float halfTexel = 1.0f;
            float lod = 0.0f;
            std::uint32_t padding0 = 0;
        };
    } // namespace

    SkyImages::SkyImages(GraphicsDevice& device, ILogger& logger) : m_device(device), m_logger(logger)
    {
        // Round a panorama's sides, clamped at its top and bottom.
        SamplerDesc sampler;
        sampler.addressU = AddressMode::Repeat;
        sampler.addressV = AddressMode::ClampToEdge;
        sampler.addressW = AddressMode::ClampToEdge;
        m_sampler = m_device.CreateSampler(sampler, "Panorama sampler");
        m_program = ReadBuiltin(BuiltinProgram::Panorama, m_logger);
        m_layout = m_device.CreateBindingGroupLayout(
            {{{0, ShaderBindingKind::UniformBuffer}, {1, ShaderBindingKind::Texture}}}, "Panorama");
        PipelineDesc desc;
        desc.shader = m_program.FindVariant({});
        desc.sets.at(PanoramaSet) = m_layout;
        desc.colourFormats = {TextureFormat::Rgba16Float};
        m_pipeline = MakeFullscreenPipeline(m_device, m_logger, desc, m_program.name);
    }

    SkyImages::~SkyImages()
    {
        for (auto& [key, cubemap] : m_cubemaps)
        {
            m_device.Destroy(cubemap.texture);
        }
        while (!m_panoramas.empty())
        {
            ReleasePanorama(m_panoramas.begin()->first);
        }
        m_device.Destroy(m_pipeline);
        m_device.Destroy(m_layout);
        m_device.Destroy(m_sampler);
    }

    bool SkyImages::IsCubemapCurrent(AssetKey key, std::uint32_t revision) const
    {
        const auto found = m_cubemaps.find(key);
        return found != m_cubemaps.end() && found->second.revision == revision;
    }

    void SkyImages::PrepareCubemap(AssetKey key, std::uint32_t revision, const ImageData& image)
    {
        ReleaseCubemap(key);
        CubemapEntry& entry = m_cubemaps[key];
        entry.revision = revision;
        TextureDesc desc;
        desc.kind = TextureKind::Cube;
        desc.format = TextureFormatOf(image);
        desc.width = image.width;
        desc.height = image.height;
        desc.mipLevels = MipCount(image.width, image.height);
        desc.usage = TextureUsage::Sampled | TextureUsage::CopyDestination;
        entry.texture = m_device.CreateTexture(desc, "Sky cubemap");
        entry.desc = desc;
        const TexelCodec codec = CodecOf(image);
        const std::size_t faceBytes = std::size_t{image.width} * image.height * codec.bytes;
        for (std::uint32_t face = 0; face < 6; ++face)
        {
            WriteWithMips(m_device, entry.texture, face, desc.mipLevels,
                          std::span(image.pixels).subspan(face * faceBytes, faceBytes), image.width, image.height,
                          codec);
        }
    }

    void SkyImages::ReleaseCubemap(AssetKey key)
    {
        const auto found = m_cubemaps.find(key);
        if (found != m_cubemaps.end())
        {
            m_device.Destroy(found->second.texture);
            m_cubemaps.erase(found);
        }
    }

    std::vector<AssetKey> SkyImages::CubemapKeys() const
    {
        std::vector<AssetKey> keys;
        for (const auto& [key, entry] : m_cubemaps)
        {
            keys.push_back(key);
        }
        return keys;
    }

    bool SkyImages::IsPanoramaCurrent(AssetKey key, std::uint32_t revision) const
    {
        const auto found = m_panoramas.find(key);
        return found != m_panoramas.end() && found->second.revision == revision;
    }

    void SkyImages::PreparePanorama(AssetKey key, std::uint32_t revision, const ImageData& image)
    {
        ReleasePanorama(key);
        PanoramaEntry& entry = m_panoramas[key];
        entry.revision = revision;

        TextureDesc source;
        source.format = TextureFormatOf(image);
        source.width = image.width;
        source.height = image.height;
        source.mipLevels = MipCount(image.width, image.height);
        source.usage = TextureUsage::Sampled | TextureUsage::CopyDestination;
        entry.source = m_device.CreateTexture(source, "Panorama");
        WriteWithMips(m_device, entry.source, 0, source.mipLevels, image.pixels, image.width, image.height,
                      CodecOf(image));

        // Faces half as wide as the panorama is high, so as many texels go round the cube's horizon as round the
        // panorama's: four faces across its width. RGBA16F holds an HDR sky in half the memory of RGBA32F.
        const std::uint32_t side = std::max(image.height / 2, 1u);
        TextureDesc& cube = entry.cubeDesc;
        cube.kind = TextureKind::Cube;
        cube.format = TextureFormat::Rgba16Float;
        cube.width = side;
        cube.height = side;
        cube.mipLevels = MipCount(side, side);
        cube.usage = TextureUsage::Sampled | TextureUsage::ColourAttachment;
        entry.cube = m_device.CreateTexture(cube, "Sky panorama cube");

        // Each pass's block: its face, half its level's texel, and the panorama's mip whose texels are as tall as the
        // level's (a face spans half the panorama's height).
        std::vector<PanoramaBlock> blocks;
        for (std::uint32_t level = 0; level < cube.mipLevels; ++level)
        {
            const std::uint32_t levelSide = std::max(side >> level, 1u);
            const float lod = std::max(std::log2(static_cast<float>(image.height) / (2.0f * levelSide)), 0.0f);
            for (std::uint32_t face = 0; face < 6; ++face)
            {
                blocks.push_back({face, 1.0f / static_cast<float>(levelSide), lod});
            }
        }
        entry.params = MakeAlignedBlocks(m_device, BlocksOf(blocks), "Panorama passes");
        for (std::size_t pass = 0; pass < blocks.size(); ++pass)
        {
            BindingGroupEntry params;
            params.binding = 0;
            params.buffer = entry.params;
            params.offset = UniformBlockAlignment * pass;
            params.size = sizeof(PanoramaBlock);
            BindingGroupEntry panorama;
            panorama.binding = 1;
            panorama.texture = entry.source;
            panorama.sampler = m_sampler;
            entry.passGroups.push_back(m_device.CreateBindingGroup({m_layout, {params, panorama}}, "Panorama"));
        }
    }

    void SkyImages::ReleasePanorama(AssetKey key)
    {
        const auto found = m_panoramas.find(key);
        if (found == m_panoramas.end())
        {
            return;
        }
        PanoramaEntry& entry = found->second;
        FreePasses(entry);
        m_device.Destroy(entry.cube);
        m_panoramas.erase(found);
    }

    std::vector<AssetKey> SkyImages::PanoramaKeys() const
    {
        std::vector<AssetKey> keys;
        for (const auto& [key, entry] : m_panoramas)
        {
            keys.push_back(key);
        }
        return keys;
    }

    void SkyImages::FreePasses(PanoramaEntry& entry)
    {
        for (const BindingGroupHandle group : entry.passGroups)
        {
            m_device.Destroy(group);
        }
        entry.passGroups.clear();
        m_device.Destroy(entry.params);
        m_device.Destroy(entry.source);
        entry.params = {};
        entry.source = {};
    }

    void SkyImages::AddPasses(RenderGraph& graph)
    {
        for (auto& [key, entry] : m_panoramas)
        {
            if (entry.converted)
            {
                // Its passes ran in an earlier frame, so the image's texture can go.
                FreePasses(entry);
                continue;
            }
            if (m_pipeline.IsEmpty())
            {
                continue;
            }
            const GraphResource cube = graph.ImportTexture("Sky panorama cube", entry.cube, entry.cubeDesc);
            for (std::uint32_t level = 0; level < entry.cubeDesc.mipLevels; ++level)
            {
                const std::uint32_t side = std::max(entry.cubeDesc.width >> level, 1u);
                for (std::uint32_t face = 0; face < 6; ++face)
                {
                    const BindingGroupHandle group = entry.passGroups.at(std::size_t{level} * 6 + face);
                    graph.AddPass(
                        "Panorama", RenderStage::Prepare, QueueKind::Graphics,
                        [=](PassBuilder& pass) { pass.ColourAttachmentLevel(cube, level, face); },
                        [this, key, group, side](PassContext& context) {
                            PassEncoder& encoder = context.Encoder();
                            encoder.SetViewport({0, 0, side, side});
                            encoder.SetPipeline(m_pipeline);
                            encoder.SetBindingGroup(PanoramaSet, group);
                            encoder.Draw({.vertexCount = 3});
                            // Recorded in this frame's graph, which runs before any Prepare or Release can come.
                            m_panoramas.at(key).converted = true;
                        });
                }
            }
        }
    }

    SkyCube SkyImages::Cube(AssetKey key) const
    {
        if (const auto cubemap = m_cubemaps.find(key); cubemap != m_cubemaps.end())
        {
            return {cubemap->second.texture, cubemap->second.desc};
        }
        // Converted in an earlier frame, or by this frame's Panorama passes, which run first in Prepare. One whose
        // passes cannot run has nothing in its cube texture.
        const auto panorama = m_panoramas.find(key);
        if (panorama == m_panoramas.end() || (!panorama->second.converted && m_pipeline.IsEmpty()))
        {
            return {};
        }
        return {panorama->second.cube, panorama->second.cubeDesc};
    }
} // namespace Fenrir::Render
