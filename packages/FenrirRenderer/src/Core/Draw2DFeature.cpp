#include "Core/Draw2DFeature.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <numeric>
#include <span>
#include <string_view>
#include <utility>

#include "Core/FrameRingBuffer.hpp"
#include "FenrirLogger/ILogger.hpp"

namespace Fenrir::Render
{
    namespace
    {
        using namespace Fenrir::Graphics;

        /** @brief The binding set of a 2D shader's command: its parameter block, then its textures. */
        constexpr std::uint32_t CommandSet = 1;

        /** @brief Uniform blocks are bound at multiples of this, the largest alignment GL implementations ask for. */
        constexpr std::size_t UniformAlignment = 256;

        /** @brief The target block, as `Fenrir/2D.glsl` declares it (std140). */
        struct TargetBlock
        {
            Math::Mat4 localToClip;
            Math::Vec4 pixelSize;
            Math::Vec4 contentScale;
        };
        static_assert(sizeof(TargetBlock) == 96);

        constexpr std::size_t AlignUp(std::size_t value, std::size_t alignment)
        {
            return (value + alignment - 1) / alignment * alignment;
        }

        // The 2D vertex formats are the device's, in the same order.
        static_assert(static_cast<int>(VertexFormat2D::Float) == static_cast<int>(VertexFormat::Float) &&
                      static_cast<int>(VertexFormat2D::Float2) == static_cast<int>(VertexFormat::Float2) &&
                      static_cast<int>(VertexFormat2D::Float3) == static_cast<int>(VertexFormat::Float3) &&
                      static_cast<int>(VertexFormat2D::Float4) == static_cast<int>(VertexFormat::Float4) &&
                      static_cast<int>(VertexFormat2D::Unorm8x4) == static_cast<int>(VertexFormat::Unorm8x4));

        /** @brief ADR-0027's blend modes as the device's, whose factors are the ADR's table, by BlendMode2D. */
        constexpr std::array<Graphics::BlendMode, 4> deviceBlends{
            Graphics::BlendMode::Alpha, Graphics::BlendMode::Premultiplied, Graphics::BlendMode::Additive,
            Graphics::BlendMode::None};

        /** @brief Each TextureFormat's name, for the log, in its order. */
        constexpr std::array<std::string_view, 6> formatNames{"RGBA8", "RGBA8 sRGB", "RGBA16F",
                                                              "depth", "RGBA32F",    "R8"};

        /** @brief The default texture of the texture parameter `name`; white if the variant does not list it. */
        DefaultTexture DefaultOf(const ShaderVariant& variant, std::string_view name)
        {
            const auto found = std::find_if(variant.parameters.begin(), variant.parameters.end(), [&](const auto& p) {
                return p.type == ShaderParameterType::Texture && p.name == name;
            });
            return found == variant.parameters.end() ? DefaultTexture::White : found->texture;
        }

        /**
         * @brief `rect`, in local units, as the target's pixels: scaled by `scale`, rounded outwards to whole pixels
         * and clamped to `pixels`. Zero wide or high when it covers no pixel.
         */
        PixelRect ScissorPixels(const Rect2D& rect, float scale, PixelSize pixels)
        {
            const auto clampTo = [](float value, std::uint32_t limit) {
                return static_cast<std::int64_t>(std::clamp(value, 0.0f, static_cast<float>(limit)));
            };
            const std::int64_t left = clampTo(std::floor(rect.x * scale), pixels.width);
            const std::int64_t top = clampTo(std::floor(rect.y * scale), pixels.height);
            const std::int64_t right = clampTo(std::ceil((rect.x + rect.width) * scale), pixels.width);
            const std::int64_t bottom = clampTo(std::ceil((rect.y + rect.height) * scale), pixels.height);
            return {static_cast<std::int32_t>(left), static_cast<std::int32_t>(top),
                    static_cast<std::uint32_t>(std::max<std::int64_t>(right - left, 0)),
                    static_cast<std::uint32_t>(std::max<std::int64_t>(bottom - top, 0))};
        }
    } // namespace

    class Draw2DFeature::ByteRing
    {
      public:
        ByteRing(GraphicsDevice& device, BufferUsage usage, std::string label)
            : m_device(device), m_usage(usage), m_label(std::move(label))
        {
        }

        /** @brief Writes `bytes` from the start of frame `frameIndex`'s buffer, remaking the buffers if they are small.
         */
        void Write(std::uint64_t frameIndex, std::span<const std::byte> bytes)
        {
            if (bytes.size() > m_capacity || m_ring == nullptr)
            {
                // Grow by doubling, so a UI that keeps growing remakes its buffers rarely.
                m_capacity = std::max({bytes.size(), m_capacity * 2, std::size_t{4096}});
                m_ring.reset();
                m_ring = std::make_unique<FrameRingBuffer>(m_device, BufferDesc{m_capacity, m_usage}, m_label);
            }
            m_ring->Write(frameIndex, 0, bytes);
        }

        [[nodiscard]] BufferHandle Current(std::uint64_t frameIndex) const
        {
            return m_ring == nullptr ? BufferHandle{} : m_ring->Current(frameIndex);
        }

      private:
        GraphicsDevice& m_device;
        BufferUsage m_usage;
        std::string m_label;
        std::size_t m_capacity = 0;
        std::unique_ptr<FrameRingBuffer> m_ring;
    };

    Draw2DFeature::Draw2DFeature(GraphicsDevice& device, ILogger& logger, SceneRenderer& images)
        : m_device(device), m_logger(logger), m_images(images)
    {
        m_targetLayout = m_device.CreateBindingGroupLayout({{{0, ShaderBindingKind::UniformBuffer}}}, "2D target");
        m_targetBlocks = std::make_unique<BlockRing>(m_device, m_targetLayout, false, sizeof(TargetBlock), "2D target");
        m_vertices = std::make_unique<ByteRing>(m_device, BufferUsage::Vertex, "2D vertices");
        m_indices = std::make_unique<ByteRing>(m_device, BufferUsage::Index, "2D indices");
        m_parameters = std::make_unique<ByteRing>(m_device, BufferUsage::Uniform, "2D parameters");
    }

    Draw2DFeature::~Draw2DFeature()
    {
        for (const BindingGroupHandle group : m_frameGroups)
        {
            m_device.Destroy(group);
        }
        for (const auto& [key, shader] : m_shaders)
        {
            DestroyPipelinesOf(key);
            for (const auto& [variant, layout] : shader.layouts)
            {
                m_device.Destroy(layout);
            }
        }
        m_targetBlocks.reset();
        m_vertices.reset();
        m_indices.reset();
        m_parameters.reset();
        m_device.Destroy(m_targetLayout);
    }

    // ---- Prepare ----

    bool Draw2DFeature::IsShaderCurrent(AssetKey key, std::uint32_t revision) const
    {
        const auto found = m_shaders.find(key);
        return found != m_shaders.end() && found->second.revision == revision;
    }

    void Draw2DFeature::PrepareShader(AssetKey key, std::uint32_t revision, const ShaderData& shader)
    {
        ReleaseShader(key);
        ShaderEntry& entry = m_shaders[key];
        entry.revision = revision;
        ShaderContainerResult read = ReadShaderContainer(shader.bytes);
        if (!read.Ok() || read.container->materialSet != CommandSet)
        {
            m_logger.Error("FenrirRenderer: shader {} cannot draw a 2D draw list: {}", key.index,
                           read.Ok() ? std::string("it is not a 2D shader (its set 0 is not Fenrir/2D.glsl's target)")
                                     : read.diagnostic);
            return;
        }
        entry.container = std::make_unique<ShaderContainer>(std::move(*read.container));
        for (const ShaderVariant& variant : entry.container->variants)
        {
            BindingGroupLayoutDesc layout;
            for (const ShaderBinding& binding : variant.bindings)
            {
                if (binding.set == CommandSet)
                {
                    layout.entries.push_back({binding.binding, binding.kind});
                }
            }
            entry.layouts[&variant] = layout.entries.empty()
                                          ? BindingGroupLayoutHandle{}
                                          : m_device.CreateBindingGroupLayout(layout, entry.container->name);
        }
    }

    void Draw2DFeature::ReleaseShader(AssetKey key)
    {
        const auto found = m_shaders.find(key);
        if (found == m_shaders.end())
        {
            return;
        }
        DestroyPipelinesOf(key);
        for (const auto& [variant, layout] : found->second.layouts)
        {
            m_device.Destroy(layout);
        }
        m_shaders.erase(found);
    }

    std::vector<AssetKey> Draw2DFeature::ShaderKeys() const
    {
        std::vector<AssetKey> keys;
        for (const auto& [key, entry] : m_shaders)
        {
            keys.push_back(key);
        }
        return keys;
    }

    void Draw2DFeature::DestroyPipelinesOf(AssetKey shader)
    {
        std::erase_if(m_pipelines, [&](const PipelineEntry& entry) {
            if (KeyOf(entry.description.shader) != shader)
            {
                return false;
            }
            m_device.Destroy(entry.pipeline);
            return true;
        });
    }

    // ---- Each frame ----

    void Draw2DFeature::SetFrame(OverlayFrame frame)
    {
        m_frame = std::move(frame);
    }

    void Draw2DFeature::LogOnce(const std::string& reason, const std::string& message)
    {
        if (m_logged.insert(reason).second)
        {
            m_logger.Error("FenrirRenderer: {}", message);
        }
    }

    const Draw2DFeature::PipelineEntry* Draw2DFeature::Pipeline(const PipelineDescription2D& description,
                                                                TextureFormat format)
    {
        const auto cached = std::find_if(m_pipelines.begin(), m_pipelines.end(), [&](const PipelineEntry& entry) {
            return entry.format == format && entry.description == description;
        });
        if (cached != m_pipelines.end())
        {
            return cached->pipeline.IsEmpty() ? nullptr : &*cached;
        }

        const ShaderEntry& shader = m_shaders.at(KeyOf(description.shader));
        PipelineEntry& entry = m_pipelines.emplace_back();
        entry.description = description;
        entry.format = format;

        bool gamma = description.blendSpace == BlendSpace2D::Gamma;
        if (gamma && format != TextureFormat::Rgba8Srgb)
        {
            m_logger.Error("FenrirRenderer: a 2D pipeline description with shader {} asks for the Gamma blend space, "
                           "which needs an 8-bit sRGB target, but its target is {}; it draws as Linear",
                           description.shader.index, formatNames.at(static_cast<std::size_t>(format)));
            gamma = false;
        }
        std::vector<std::string_view> keys;
        if (description.inputColourSpace == ColourSpace::Srgb)
        {
            keys.emplace_back("INPUT_SRGB");
        }
        if (description.blendMode == BlendMode2D::PremultipliedAlpha)
        {
            keys.emplace_back("PREMULTIPLY");
        }
        if (gamma)
        {
            keys.emplace_back("GAMMA");
        }
        entry.variant = shader.container->FindVariant(keys);
        if (entry.variant == nullptr)
        {
            return nullptr;
        }
        entry.layout = shader.layouts.at(entry.variant);

        PipelineDesc desc;
        desc.shader = entry.variant;
        desc.sets = {m_targetLayout, entry.layout, BindingGroupLayoutHandle{}, BindingGroupLayoutHandle{}};
        VertexBufferLayout vertices;
        vertices.stride = description.vertexLayout.stride;
        for (const VertexAttribute2D& attribute : description.vertexLayout.attributes)
        {
            vertices.attributes.push_back(
                {attribute.location, static_cast<VertexFormat>(attribute.format), attribute.offset});
        }
        desc.vertexBuffers = {vertices};
        // The window overlay's rules: no depth and no culling (ADR-0027).
        desc.cull = CullMode::None;
        desc.depth.test = false;
        desc.depth.write = false;
        desc.depthFormat.reset();
        desc.blend = deviceBlends.at(static_cast<std::size_t>(description.blendMode));
        desc.encodeSrgb = !gamma;
        desc.colourFormats = {format};
        desc.samples = 1;
        const PipelineResult made = m_device.CreatePipeline(desc, shader.container->name);
        entry.pipeline = made.pipeline;
        if (entry.pipeline.IsEmpty())
        {
            m_logger.Error("FenrirRenderer: the 2D pipeline for shader '{}' could not be made: {}",
                           shader.container->name, made.diagnostic);
            return nullptr;
        }
        return &entry;
    }

    bool Draw2DFeature::Resolve(const DrawList2D& list, const Command2D& command, std::size_t vertexOffset,
                                std::size_t indexOffset, TextureFormat format, PixelSize pixels, float scale,
                                std::vector<Draw>& draws)
    {
        if (command.pipeline >= list.pipelines.size())
        {
            LogOnce("pipeline", std::format("a 2D draw list's command names pipeline {} of a table of {}, so it is "
                                            "skipped",
                                            command.pipeline, list.pipelines.size()));
            return false;
        }
        if (command.textures.size() > MaxTextureSlots2D)
        {
            LogOnce("textures", std::format("a 2D draw list's command has {} texture slots, more than {}, so it is "
                                            "skipped",
                                            command.textures.size(), MaxTextureSlots2D));
            return false;
        }
        if (command.parameters.size() > MaxParameterBytes2D)
        {
            LogOnce("parameters", std::format("a 2D draw list's command has {} parameter bytes, more than {}, so it is "
                                              "skipped",
                                              command.parameters.size(), MaxParameterBytes2D));
            return false;
        }
        const PipelineDescription2D& description = list.pipelines[command.pipeline];
        const std::size_t indexSize = description.indexType == IndexType2D::UInt16 ? 2 : 4;
        if ((std::size_t{command.indexOffset} + command.indexCount) * indexSize > list.indices.size())
        {
            LogOnce("indices", std::format("a 2D draw list's command draws indices {} to {}, past its {} indices, so "
                                           "it is skipped",
                                           command.indexOffset, command.indexOffset + command.indexCount,
                                           list.indices.size() / indexSize));
            return false;
        }
        const auto shader = m_shaders.find(KeyOf(description.shader));
        if (shader == m_shaders.end())
        {
            LogOnce(std::format("shader {} {}", description.shader.index, description.shader.generation),
                    std::format("a 2D pipeline description names {} shader ({}), so its commands are skipped",
                                description.shader.IsEmpty() ? "no" : "a stale", description.shader.index));
            return false;
        }
        if (shader->second.container == nullptr || command.indexCount == 0)
        {
            return false;
        }
        Draw draw;
        draw.scissor = command.scissor ? ScissorPixels(*command.scissor, scale, pixels)
                                       : PixelRect{0, 0, pixels.width, pixels.height};
        if (draw.scissor.width == 0 || draw.scissor.height == 0)
        {
            return false;
        }
        const PipelineEntry* pipeline = Pipeline(description, format);
        if (pipeline == nullptr)
        {
            return false;
        }

        // Set 1: the parameter block, then the textures, slot by slot, in binding order.
        const ShaderVariant& variant = *pipeline->variant;
        if (!pipeline->layout.IsEmpty())
        {
            BindingGroupDesc group{pipeline->layout, {}};
            std::size_t slot = 0;
            for (const ShaderBinding& binding : variant.bindings)
            {
                if (binding.set != CommandSet)
                {
                    continue;
                }
                BindingGroupEntry entry;
                entry.binding = binding.binding;
                if (binding.kind == ShaderBindingKind::Texture)
                {
                    const TextureSlot2D texture =
                        slot < command.textures.size() ? command.textures[slot] : TextureSlot2D{};
                    ++slot;
                    entry.texture = m_images.SampledTexture(KeyOf(texture.image));
                    if (entry.texture.IsEmpty())
                    {
                        entry.texture = m_images.GetDefaultTexture(DefaultOf(variant, binding.name));
                    }
                    entry.sampler = m_images.SamplerFor(texture.sampler);
                }
                else if (variant.materialBlockBinding == binding.binding)
                {
                    // The block's parameter defaults, with the command's bytes over its start.
                    std::vector<std::byte> block = variant.defaultBlock;
                    block.resize(std::max<std::size_t>(binding.size, block.size()));
                    if (!command.parameters.empty())
                    {
                        std::memcpy(block.data(), command.parameters.data(),
                                    std::min(block.size(), command.parameters.size()));
                    }
                    // Its buffer is set once the frame's parameter bytes are written.
                    entry.offset = m_parameterBytes.size();
                    entry.size = block.size();
                    m_parameterBytes.insert(m_parameterBytes.end(), block.begin(), block.end());
                    m_parameterBytes.resize(AlignUp(m_parameterBytes.size(), UniformAlignment));
                    m_parameterEntries.push_back({m_pendingGroups.size(), group.entries.size()});
                }
                group.entries.push_back(entry);
            }
            m_pendingGroups.push_back(std::move(group));
            draw.groupIndex = m_pendingGroups.size();
        }
        draw.pipeline = pipeline->pipeline;
        draw.vertexOffset = vertexOffset;
        draw.indexOffset = indexOffset;
        draw.indexFormat = description.indexType == IndexType2D::UInt16 ? IndexFormat::UInt16 : IndexFormat::UInt32;
        draw.firstIndex = command.indexOffset;
        draw.indexCount = command.indexCount;
        draw.baseVertex = command.baseVertex;
        draws.push_back(draw);
        return true;
    }

    void Draw2DFeature::AddPasses(RenderGraph& graph, const FrameContext& frame)
    {
        for (const BindingGroupHandle group : m_frameGroups)
        {
            m_device.Destroy(group);
        }
        m_frameGroups.clear();
        OverlayFrame overlay = std::move(m_frame);
        m_frame = {};
        if (overlay.submissions.empty())
        {
            return;
        }

        GraphResource target = frame.backbuffer;
        TextureFormat format = TextureFormat::Rgba8Srgb;
        PixelSize pixels = frame.size;
        if (!overlay.target.IsEmpty())
        {
            target = graph.ImportTexture("2D target", overlay.target, overlay.targetDesc);
            format = overlay.targetDesc.format;
            pixels = {overlay.targetDesc.width, overlay.targetDesc.height};
        }
        const float scale = overlay.contentScale > 0.0f ? overlay.contentScale : 1.0f;
        const std::uint64_t frameIndex = frame.frameIndex;

        // Local units, top-left origin and Y down, over the window's logical size, to clip space, Y up (ADR-0027).
        const Math::Vec2 extent =
            Math::Vec2(static_cast<float>(pixels.width), static_cast<float>(pixels.height)) / scale;
        Math::Mat4 localToClip(1.0f);
        localToClip[0][0] = 2.0f / extent.x;
        localToClip[1][1] = -2.0f / extent.y;
        localToClip[2][2] = 0.0f;
        localToClip[3] = Math::Vec4(-1.0f, 1.0f, 0.5f, 1.0f);
        const TargetBlock block{localToClip,
                                Math::Vec4(static_cast<float>(pixels.width), static_cast<float>(pixels.height), 0, 0),
                                Math::Vec4(scale, 0.0f, 0.0f, 0.0f)};
        m_targetBlocks->Reserve(1);
        m_targetBlocks->Write(frameIndex, 0, std::as_bytes(std::span(&block, 1)));

        // Layers lowest first, then submission order.
        std::vector<std::size_t> order(overlay.submissions.size());
        std::iota(order.begin(), order.end(), std::size_t{0});
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return overlay.submissions[a].layer < overlay.submissions[b].layer;
        });

        std::vector<std::byte> vertexBytes;
        std::vector<std::byte> indexBytes;
        m_parameterBytes.clear();
        m_parameterEntries.clear();
        m_pendingGroups.clear();
        std::vector<Draw> draws;
        for (const std::size_t s : order)
        {
            const DrawList2D& list = overlay.submissions[s].list;
            const std::size_t vertexOffset = AlignUp(vertexBytes.size(), 16);
            const std::size_t indexOffset = AlignUp(indexBytes.size(), 4);
            vertexBytes.resize(vertexOffset);
            vertexBytes.insert(vertexBytes.end(), list.vertices.begin(), list.vertices.end());
            indexBytes.resize(indexOffset);
            indexBytes.insert(indexBytes.end(), list.indices.begin(), list.indices.end());
            for (const Command2D& command : list.commands)
            {
                Resolve(list, command, vertexOffset, indexOffset, format, pixels, scale, draws);
            }
        }
        if (draws.empty())
        {
            return;
        }

        m_vertices->Write(frameIndex, vertexBytes);
        m_indices->Write(frameIndex, indexBytes);
        if (!m_parameterBytes.empty())
        {
            m_parameters->Write(frameIndex, m_parameterBytes);
            for (const auto& [group, entry] : m_parameterEntries)
            {
                m_pendingGroups.at(group).entries.at(entry).buffer = m_parameters->Current(frameIndex);
            }
        }
        std::vector<BindingGroupHandle> groups;
        for (const BindingGroupDesc& desc : m_pendingGroups)
        {
            groups.push_back(m_device.CreateBindingGroup(desc, "2D command"));
        }
        m_frameGroups = groups;
        for (Draw& draw : draws)
        {
            if (draw.groupIndex > 0)
            {
                draw.group = groups.at(draw.groupIndex - 1);
            }
        }

        const BufferHandle vertexBuffer = m_vertices->Current(frameIndex);
        const BufferHandle indexBuffer = m_indices->Current(frameIndex);
        const BindingGroupHandle targetGroup = m_targetBlocks->Group(frameIndex, 0);
        const PixelRect viewport{0, 0, pixels.width, pixels.height};
        // Before the debug feature's Overlay pass, which draws debug screen text as the fixed top layer.
        graph.AddPass(
            "Overlay 2D", RenderStage::Overlay, QueueKind::Graphics,
            [=](PassBuilder& pass) { pass.ColourAttachment(target, LoadOp::Load); },
            [recorded = std::move(draws), viewport, vertexBuffer, indexBuffer, targetGroup](PassContext& context) {
                PassEncoder& encoder = context.Encoder();
                encoder.SetViewport(viewport);
                for (const Draw& draw : recorded)
                {
                    encoder.SetPipeline(draw.pipeline);
                    encoder.SetBindingGroup(0, targetGroup);
                    if (!draw.group.IsEmpty())
                    {
                        encoder.SetBindingGroup(1, draw.group);
                    }
                    encoder.SetVertexBuffer(0, vertexBuffer, draw.vertexOffset);
                    encoder.SetIndexBuffer(indexBuffer, draw.indexFormat, draw.indexOffset);
                    encoder.SetScissor(draw.scissor);
                    encoder.DrawIndexed(
                        {.indexCount = draw.indexCount, .firstIndex = draw.firstIndex, .baseVertex = draw.baseVertex});
                }
            });
    }
} // namespace Fenrir::Render
