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
#include <variant>

#include "Core/BlockRing.hpp"
#include "Core/BuiltinShaders.hpp"
#include "Core/FrameRingBuffer.hpp"
#include "Core/UniformBlocks.hpp"
#include "FenrirLogger/ILogger.hpp"

namespace Fenrir::Render
{
    namespace
    {
        using namespace Fenrir::Graphics;

        /** @brief The binding set of a 2D shader's command: its parameter block, then its textures. */
        constexpr std::uint32_t CommandSet = 1;

        /** @brief The target block, as `Fenrir/2D.glsl` declares it (std140). */
        struct TargetBlock
        {
            Math::Mat4 localToClip;
            Math::Vec4 pixelSize;
            Math::Vec4 contentScale;
        };
        static_assert(sizeof(TargetBlock) == 96);

        /** @brief The bytes one pixel of a group's transient image takes: RGBA8. */
        constexpr std::uint32_t GroupPixelBytes = 4;

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
        constexpr std::array<std::string_view, 8> formatNames{"RGBA8",   "RGBA8 sRGB", "RGBA16F", "depth",
                                                              "RGBA32F", "R8",         "RG16F",   "R32F"};

        /** @brief The default texture of the texture parameter `name`; white if the variant does not list it. */
        DefaultTexture DefaultOf(const ShaderVariant& variant, std::string_view name)
        {
            const auto found = std::find_if(variant.parameters.begin(), variant.parameters.end(), [&](const auto& p) {
                return p.type == ShaderParameterType::Texture && p.name == name;
            });
            return found == variant.parameters.end() ? DefaultTexture::White : found->texture;
        }

        /** @brief `layout` as the device's vertex buffer layout. */
        VertexBufferLayout DeviceLayout(const VertexLayout2D& layout)
        {
            VertexBufferLayout vertices;
            vertices.stride = layout.stride;
            for (const VertexAttribute2D& attribute : layout.attributes)
            {
                vertices.attributes.push_back(
                    {attribute.location, static_cast<VertexFormat>(attribute.format), attribute.offset});
            }
            return vertices;
        }

        /** @brief A rectangle of a root target's pixels, from its left, top, right and bottom edges. */
        struct PixelEdges
        {
            std::int64_t left = 0;
            std::int64_t top = 0;
            std::int64_t right = 0;
            std::int64_t bottom = 0;
        };

        /** @brief `rect`, in local units, as a root target's pixels: scaled by `scale` and rounded outwards. */
        PixelEdges RoundOut(const Rect2D& rect, float scale)
        {
            return {static_cast<std::int64_t>(std::floor(rect.x * scale)),
                    static_cast<std::int64_t>(std::floor(rect.y * scale)),
                    static_cast<std::int64_t>(std::ceil((rect.x + rect.width) * scale)),
                    static_cast<std::int64_t>(std::ceil((rect.y + rect.height) * scale))};
        }

        /**
         * @brief `rect`, in local units, as the pixels of a target of `pixels` whose top-left is (`originX`, `originY`)
         * in its root's: scaled by `scale`, rounded outwards to whole pixels and clamped to the target. Zero wide or
         * high when it covers no pixel.
         */
        PixelRect ScissorPixels(const Rect2D& rect, float scale, std::int32_t originX, std::int32_t originY,
                                PixelSize pixels)
        {
            const PixelEdges edges = RoundOut(rect, scale);
            const auto clampTo = [](std::int64_t value, std::uint32_t limit) {
                return std::clamp<std::int64_t>(value, 0, limit);
            };
            const std::int64_t left = clampTo(edges.left - originX, pixels.width);
            const std::int64_t top = clampTo(edges.top - originY, pixels.height);
            const std::int64_t right = clampTo(edges.right - originX, pixels.width);
            const std::int64_t bottom = clampTo(edges.bottom - originY, pixels.height);
            return {static_cast<std::int32_t>(left), static_cast<std::int32_t>(top),
                    static_cast<std::uint32_t>(std::max<std::int64_t>(right - left, 0)),
                    static_cast<std::uint32_t>(std::max<std::int64_t>(bottom - top, 0))};
        }

        /** @brief Whether `list`'s groups and masks balance: each end closes what is open, and nothing stays open. */
        bool Balanced(const DrawList2D& list)
        {
            // What is open, innermost last: a group (false) or a mask's reach (true).
            std::vector<bool> open;
            for (const Command2D& command : list.commands)
            {
                if (std::holds_alternative<BeginGroup2D>(command))
                {
                    open.push_back(false);
                }
                else if (const auto* end = std::get_if<EndGroup2D>(&command))
                {
                    if (open.empty() || open.back())
                    {
                        return false;
                    }
                    open.pop_back();
                    if (end->mask)
                    {
                        open.push_back(true);
                    }
                }
                else if (std::holds_alternative<EndMask2D>(command))
                {
                    if (open.empty() || !open.back())
                    {
                        return false;
                    }
                    open.pop_back();
                }
            }
            return open.empty();
        }

        /** @brief The target block of a pass of `pixels` at `scale` whose top-left is (`originX`, `originY`). */
        TargetBlock BlockOf(PixelSize pixels, float scale, std::int32_t originX, std::int32_t originY)
        {
            // Local units, top-left origin and Y down, offset to the pass's origin, to clip space, Y up (ADR-0027).
            const float width = static_cast<float>(pixels.width);
            const float height = static_cast<float>(pixels.height);
            Math::Mat4 localToClip(1.0f);
            localToClip[0][0] = 2.0f * scale / width;
            localToClip[1][1] = -2.0f * scale / height;
            localToClip[2][2] = 0.0f;
            localToClip[3] = Math::Vec4(-1.0f - 2.0f * static_cast<float>(originX) / width,
                                        1.0f + 2.0f * static_cast<float>(originY) / height, 0.5f, 1.0f);
            return {localToClip, Math::Vec4(width, height, 0.0f, 0.0f), Math::Vec4(scale, 0.0f, 0.0f, 0.0f)};
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

        // builtin:2d with no keys: linear vertex colours, which carry the opacity, times the premultiplied texel.
        m_composite.container = std::make_unique<ShaderContainer>(ReadBuiltin(BuiltinProgram::Draw2D, m_logger));
        m_composite.variant = m_composite.container->FindVariant({});
        if (m_composite.variant != nullptr)
        {
            BindingGroupLayoutDesc layout;
            for (const ShaderBinding& binding : m_composite.variant->bindings)
            {
                if (binding.set == CommandSet)
                {
                    layout.entries.push_back({binding.binding, binding.kind});
                }
            }
            m_composite.layout = m_device.CreateBindingGroupLayout(layout, "2D composite");
        }
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
        for (const auto& [key, pipeline] : m_composite.pipelines)
        {
            m_device.Destroy(pipeline);
        }
        m_device.Destroy(m_composite.layout);
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

    void Draw2DFeature::SetFrame(Frame2D frame)
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
        desc.vertexBuffers = {DeviceLayout(description.vertexLayout)};
        // Every 2D target's rules: no depth and no culling (ADR-0027).
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

    PipelineHandle Draw2DFeature::CompositePipeline(GroupBlend2D blend, TextureFormat format)
    {
        const auto key = std::make_pair(blend, format);
        if (const auto found = m_composite.pipelines.find(key); found != m_composite.pipelines.end())
        {
            return found->second;
        }
        PipelineHandle& pipeline = m_composite.pipelines[key];
        if (m_composite.variant == nullptr)
        {
            return pipeline;
        }
        PipelineDesc desc;
        desc.shader = m_composite.variant;
        desc.sets = {m_targetLayout, m_composite.layout, BindingGroupLayoutHandle{}, BindingGroupLayoutHandle{}};
        desc.vertexBuffers = {DeviceLayout(Vertex2DLayout())};
        desc.cull = CullMode::None;
        desc.depth.test = false;
        desc.depth.write = false;
        desc.depthFormat.reset();
        // The image is premultiplied, so drawing it over is the premultiplied blend.
        desc.blend =
            blend == GroupBlend2D::Additive ? Graphics::BlendMode::Additive : Graphics::BlendMode::Premultiplied;
        desc.encodeSrgb = true;
        desc.colourFormats = {format};
        desc.samples = 1;
        const PipelineResult made = m_device.CreatePipeline(desc, "2D composite");
        pipeline = made.pipeline;
        if (pipeline.IsEmpty())
        {
            m_logger.Error("FenrirRenderer: the 2D composite pipeline could not be made: {}", made.diagnostic);
        }
        return pipeline;
    }

    bool Draw2DFeature::Resolve(const DrawList2D& list, const DrawCommand2D& command, std::size_t vertexOffset,
                                std::size_t indexOffset, const ListTarget& target, TargetPass& pass)
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
        draw.scissor = command.scissor
                           ? ScissorPixels(*command.scissor, pass.scale, pass.originX, pass.originY, pass.pixels)
                           : PixelRect{0, 0, pass.pixels.width, pass.pixels.height};
        if (draw.scissor.width == 0 || draw.scissor.height == 0)
        {
            return false;
        }
        const PipelineEntry* pipeline = Pipeline(description, pass.format);
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
                    const AssetKey image = KeyOf(texture.image);
                    if (!target.self.IsEmpty() && image == target.self)
                    {
                        // An image cannot be drawn into and sampled by the same pass.
                        LogOnce(std::format("self {} {}", image.index, image.generation),
                                std::format("a 2D draw list drawn into image {} samples that image, so it binds the "
                                            "texture's default there",
                                            image.index));
                    }
                    else
                    {
                        entry.texture = m_images.SampledTexture(image);
                        if (const auto drawn = target.images->find(image); drawn != target.images->end())
                        {
                            pass.samples.push_back(drawn->second);
                        }
                    }
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
                    m_parameterBytes.resize(AlignUp(m_parameterBytes.size(), UniformBlockAlignment));
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
        pass.draws.push_back(draw);
        return true;
    }

    void Draw2DFeature::AddComposite(const TargetPass& group, const EndGroup2D& composite, TargetPass& into)
    {
        const PipelineHandle pipeline = CompositePipeline(composite.blend, into.format);
        if (pipeline.IsEmpty())
        {
            return;
        }
        // The group's pixels, in the list's local units: the quad covers its image exactly.
        const float scale = into.scale;
        const float left = static_cast<float>(group.originX) / scale;
        const float top = static_cast<float>(group.originY) / scale;
        const float right = static_cast<float>(group.originX + static_cast<std::int64_t>(group.pixels.width)) / scale;
        const float bottom = static_cast<float>(group.originY + static_cast<std::int64_t>(group.pixels.height)) / scale;
        // The opacity scales the premultiplied texel's colour and alpha alike.
        const auto opacity =
            static_cast<std::uint32_t>(std::lround(std::clamp(composite.opacity, 0.0f, 1.0f) * 255.0f));
        const std::uint32_t colour = opacity | (opacity << 8u) | (opacity << 16u) | (opacity << 24u);
        const std::array<Vertex2D, 4> corners{{
            {{left, top}, {0.0f, 0.0f}, colour},
            {{right, top}, {1.0f, 0.0f}, colour},
            {{right, bottom}, {1.0f, 1.0f}, colour},
            {{left, bottom}, {0.0f, 1.0f}, colour},
        }};
        constexpr std::array<std::uint16_t, 6> indices{0, 2, 1, 0, 3, 2};

        Draw draw;
        draw.pipeline = pipeline;
        draw.composite = group.target;
        draw.vertexOffset = AlignUp(m_vertexBytes.size(), 16);
        m_vertexBytes.resize(draw.vertexOffset);
        const std::span<const std::byte> vertexBytes = std::as_bytes(std::span(corners));
        m_vertexBytes.insert(m_vertexBytes.end(), vertexBytes.begin(), vertexBytes.end());
        draw.indexOffset = AlignUp(m_indexBytes.size(), 4);
        m_indexBytes.resize(draw.indexOffset);
        const std::span<const std::byte> indexBytes = std::as_bytes(std::span(indices));
        m_indexBytes.insert(m_indexBytes.end(), indexBytes.begin(), indexBytes.end());
        draw.indexFormat = IndexFormat::UInt16;
        draw.scissor = {0, 0, into.pixels.width, into.pixels.height};
        draw.indexCount = static_cast<std::uint32_t>(indices.size());
        into.draws.push_back(draw);
        into.samples.push_back(group.target);
    }

    bool Draw2DFeature::DrawList(const DrawList2D& list, const ListTarget& target, RenderGraph& graph)
    {
        if (!Balanced(list))
        {
            LogOnce("unbalanced",
                    "a 2D draw list's offscreen groups and masks do not balance (an end with nothing open, "
                    "or a group or mask left open), so the list is skipped");
            return false;
        }
        ++m_stats.lists;
        m_stats.commands += static_cast<std::uint32_t>(list.commands.size());

        const std::size_t vertexOffset = AlignUp(m_vertexBytes.size(), 16);
        const std::size_t indexOffset = AlignUp(m_indexBytes.size(), 4);
        m_vertexBytes.resize(vertexOffset);
        m_vertexBytes.insert(m_vertexBytes.end(), list.vertices.begin(), list.vertices.end());
        m_indexBytes.resize(indexOffset);
        m_indexBytes.insert(m_indexBytes.end(), list.indices.begin(), list.indices.end());

        // Each group open, innermost last: realised into a pass of its own, drawn flat (too deep), or culled (covering
        // no pixel, or inside a culled group).
        enum class Open : std::uint8_t
        {
            Realised,
            Flat,
            Culled,
        };
        std::vector<std::pair<Open, std::size_t>> open;
        std::vector<std::size_t> realised{target.root};
        std::size_t culled = 0;
        for (const Command2D& command : list.commands)
        {
            if (const auto* draw = std::get_if<DrawCommand2D>(&command))
            {
                if (culled == 0)
                {
                    Resolve(list, *draw, vertexOffset, indexOffset, target, m_passes[realised.back()]);
                }
            }
            else if (const auto* begin = std::get_if<BeginGroup2D>(&command))
            {
                if (culled > 0)
                {
                    open.emplace_back(Open::Culled, 0);
                    ++culled;
                    continue;
                }
                if (realised.size() > MaxGroupDepth2D)
                {
                    LogOnce("depth", std::format("a 2D draw list's offscreen groups nest deeper than {}, so the deeper "
                                                 "ones draw flat into the group around them",
                                                 MaxGroupDepth2D));
                    open.emplace_back(Open::Flat, 0);
                    continue;
                }
                // The group's bounds in the root's pixels, rounded outwards and clamped to the pass around it.
                const TargetPass& outer = m_passes[realised.back()];
                const PixelEdges edges = RoundOut(begin->bounds, outer.scale);
                const std::int64_t left = std::max<std::int64_t>(edges.left, outer.originX);
                const std::int64_t top = std::max<std::int64_t>(edges.top, outer.originY);
                const std::int64_t right = std::min<std::int64_t>(edges.right, outer.originX + outer.pixels.width);
                const std::int64_t bottom = std::min<std::int64_t>(edges.bottom, outer.originY + outer.pixels.height);
                if (right <= left || bottom <= top)
                {
                    open.emplace_back(Open::Culled, 0);
                    ++culled;
                    continue;
                }
                TargetPass group;
                group.name = "Group 2D";
                group.load = LoadOp::Clear;
                group.format = TextureFormat::Rgba8Srgb;
                group.pixels = {static_cast<std::uint32_t>(right - left), static_cast<std::uint32_t>(bottom - top)};
                group.originX = static_cast<std::int32_t>(left);
                group.originY = static_cast<std::int32_t>(top);
                group.scale = outer.scale;
                m_passes.push_back(std::move(group));
                open.emplace_back(Open::Realised, m_passes.size() - 1);
                realised.push_back(m_passes.size() - 1);
            }
            else if (const auto* end = std::get_if<EndGroup2D>(&command))
            {
                const auto [kind, index] = open.back();
                open.pop_back();
                if (kind == Open::Culled)
                {
                    --culled;
                    continue;
                }
                if (kind == Open::Flat)
                {
                    continue;
                }
                realised.pop_back();
                TargetPass& group = m_passes[index];
                if (end->mask)
                {
                    LogOnce("mask", "a 2D draw list has a mask group, which the renderer does not realise yet, so it "
                                    "draws nothing and the commands it masks draw unmasked");
                    continue;
                }
                if (group.draws.empty())
                {
                    continue;
                }
                if (end->filter != GroupFilter2D::None)
                {
                    LogOnce("filter", "a 2D draw list's offscreen group has a filter, which the renderer does not "
                                      "realise yet, so it is drawn plainly");
                }
                TextureDesc desc;
                desc.format = group.format;
                desc.width = group.pixels.width;
                desc.height = group.pixels.height;
                desc.usage = TextureUsage::ColourAttachment | TextureUsage::Sampled;
                group.target = graph.CreateTexture("2D group", desc);
                m_finished.push_back(index);
                ++m_stats.groups;
                m_stats.transientImageBytes += group.pixels.width * group.pixels.height * GroupPixelBytes;
                AddComposite(group, *end, m_passes[realised.back()]);
            }
        }
        return true;
    }

    void Draw2DFeature::AddPasses(RenderGraph& graph, const FrameContext& frame)
    {
        for (const BindingGroupHandle group : m_frameGroups)
        {
            m_device.Destroy(group);
        }
        m_frameGroups.clear();
        m_stats = {};
        Frame2D drawn = std::move(m_frame);
        m_frame = {};
        if (drawn.submissions.empty() && drawn.images.empty())
        {
            return;
        }

        m_passes.clear();
        m_finished.clear();
        m_vertexBytes.clear();
        m_indexBytes.clear();
        m_parameterBytes.clear();
        m_parameterEntries.clear();
        m_pendingGroups.clear();

        // The images first, each a pass of its own, cleared, at a content scale of 1; an image a later list samples
        // runs before it.
        std::map<AssetKey, GraphResource> images;
        for (const ImageSubmission2D& image : drawn.images)
        {
            TargetPass root;
            root.name = "Image 2D";
            root.target = graph.ImportTexture("Image 2D", image.texture, image.desc);
            root.load = LoadOp::Clear;
            root.format = image.desc.format;
            root.pixels = {image.desc.width, image.desc.height};
            m_passes.push_back(std::move(root));
            const ListTarget target{m_passes.size() - 1, image.image, &images};
            for (const DrawList2D& list : image.lists)
            {
                DrawList(list, target, graph);
            }
            m_finished.push_back(target.root);
            images.emplace(image.image, m_passes[target.root].target);
        }

        if (!drawn.submissions.empty())
        {
            TargetPass root;
            root.name = "Overlay 2D";
            root.target = frame.backbuffer;
            root.load = LoadOp::Load;
            root.pixels = frame.size;
            if (!drawn.target.IsEmpty())
            {
                root.target = graph.ImportTexture("2D target", drawn.target, drawn.targetDesc);
                root.format = drawn.targetDesc.format;
                root.pixels = {drawn.targetDesc.width, drawn.targetDesc.height};
            }
            root.scale = drawn.contentScale > 0.0f ? drawn.contentScale : 1.0f;
            m_passes.push_back(std::move(root));
            const ListTarget target{m_passes.size() - 1, AssetKey{}, &images};

            // Layers lowest first, then submission order.
            std::vector<std::size_t> order(drawn.submissions.size());
            std::iota(order.begin(), order.end(), std::size_t{0});
            std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                return drawn.submissions[a].layer < drawn.submissions[b].layer;
            });
            for (const std::size_t s : order)
            {
                DrawList(drawn.submissions[s].list, target, graph);
            }
            if (!m_passes[target.root].draws.empty())
            {
                m_finished.push_back(target.root);
            }
        }
        if (m_finished.empty())
        {
            return;
        }

        const std::uint64_t frameIndex = frame.frameIndex;
        if (!m_vertexBytes.empty())
        {
            m_vertices->Write(frameIndex, m_vertexBytes);
            m_indices->Write(frameIndex, m_indexBytes);
        }
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

        m_targetBlocks->Reserve(m_finished.size());
        const BufferHandle vertexBuffer = m_vertices->Current(frameIndex);
        const BufferHandle indexBuffer = m_indices->Current(frameIndex);
        for (std::size_t slot = 0; slot < m_finished.size(); ++slot)
        {
            TargetPass& pass = m_passes[m_finished[slot]];
            const TargetBlock block = BlockOf(pass.pixels, pass.scale, pass.originX, pass.originY);
            m_targetBlocks->Write(frameIndex, slot, std::as_bytes(std::span(&block, 1)));
            for (Draw& draw : pass.draws)
            {
                if (draw.groupIndex > 0)
                {
                    draw.group = groups.at(draw.groupIndex - 1);
                }
            }
            std::sort(pass.samples.begin(), pass.samples.end(),
                      [](GraphResource a, GraphResource b) { return a.id < b.id; });
            pass.samples.erase(std::unique(pass.samples.begin(), pass.samples.end()), pass.samples.end());

            const BindingGroupHandle targetGroup = m_targetBlocks->Group(frameIndex, slot);
            const PixelRect viewport{0, 0, pass.pixels.width, pass.pixels.height};
            // Every 2D pass is in the Overlay stage: the overlay's after each view's Output and before the debug
            // feature's Overlay pass, an image's and a group's before what samples them, as the graph orders them.
            graph.AddPass(
                pass.name, RenderStage::Overlay, QueueKind::Graphics,
                [target = pass.target, load = pass.load, samples = pass.samples](PassBuilder& builder) {
                    builder.ColourAttachment(target, load, {0.0f, 0.0f, 0.0f, 0.0f});
                    for (const GraphResource sampled : samples)
                    {
                        builder.Sample(sampled);
                    }
                },
                [this, recorded = std::move(pass.draws), viewport, vertexBuffer, indexBuffer,
                 targetGroup](PassContext& context) {
                    PassEncoder& encoder = context.Encoder();
                    encoder.SetViewport(viewport);
                    for (const Draw& draw : recorded)
                    {
                        BindingGroupHandle group = draw.group;
                        if (!draw.composite.IsEmpty())
                        {
                            group = CompositeGroup(context.Texture(draw.composite));
                        }
                        encoder.SetPipeline(draw.pipeline);
                        encoder.SetBindingGroup(0, targetGroup);
                        if (!group.IsEmpty())
                        {
                            encoder.SetBindingGroup(1, group);
                        }
                        encoder.SetVertexBuffer(0, vertexBuffer, draw.vertexOffset);
                        encoder.SetIndexBuffer(indexBuffer, draw.indexFormat, draw.indexOffset);
                        encoder.SetScissor(draw.scissor);
                        encoder.DrawIndexed({.indexCount = draw.indexCount,
                                             .firstIndex = draw.firstIndex,
                                             .baseVertex = draw.baseVertex});
                    }
                });
        }
    }

    BindingGroupHandle Draw2DFeature::CompositeGroup(TextureHandle image)
    {
        BindingGroupDesc desc{m_composite.layout, {}};
        for (const ShaderBinding& binding : m_composite.variant->bindings)
        {
            if (binding.set != CommandSet)
            {
                continue;
            }
            BindingGroupEntry entry;
            entry.binding = binding.binding;
            if (binding.kind == ShaderBindingKind::Texture)
            {
                entry.texture = image;
                entry.sampler = m_images.SamplerFor(LinearClamp2D);
            }
            desc.entries.push_back(entry);
        }
        const BindingGroupHandle group = m_device.CreateBindingGroup(desc, "2D composite");
        m_frameGroups.push_back(group);
        return group;
    }
} // namespace Fenrir::Render
