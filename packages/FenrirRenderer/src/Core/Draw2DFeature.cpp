#include "Core/Draw2DFeature.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <numeric>
#include <span>
#include <string>
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

        /** @brief For each of a balanced `list`'s commands that begins a group, the index of the command ending it. */
        std::vector<std::size_t> GroupEnds(const DrawList2D& list)
        {
            std::vector<std::size_t> ends(list.commands.size(), 0);
            std::vector<std::size_t> open;
            for (std::size_t i = 0; i < list.commands.size(); ++i)
            {
                if (std::holds_alternative<BeginGroup2D>(list.commands[i]))
                {
                    open.push_back(i);
                }
                else if (std::holds_alternative<EndGroup2D>(list.commands[i]))
                {
                    ends[open.back()] = i;
                    open.pop_back();
                }
            }
            return ends;
        }

        /** @brief How far a blur of `sigma` pixels reaches: three of them, in whole pixels. */
        std::int64_t BlurReach(float sigma)
        {
            return sigma > 0.0f ? static_cast<std::int64_t>(std::ceil(3.0f * sigma)) : 0;
        }

        /** @brief `colour`, linear and premultiplied, as Vertex2D packs it. */
        std::uint32_t PackLinear(Math::Vec4 colour)
        {
            const auto byte = [](float channel) {
                return static_cast<std::uint32_t>(std::lround(std::clamp(channel, 0.0f, 1.0f) * 255.0f));
            };
            return byte(colour.x) | (byte(colour.y) << 8u) | (byte(colour.z) << 16u) | (byte(colour.w) << 24u);
        }

        /** @brief The keys of builtin:2d's variant for each composite kind, by Draw2DFeature's CompositeKind. */
        const std::array<std::vector<std::string_view>, 3> compositeKeys{
            std::vector<std::string_view>{}, std::vector<std::string_view>{"BLUR"},
            std::vector<std::string_view>{"BLUR", "SHADOW"}};

        /**
         * @brief A 2D pipeline's depth, formats and samples for a pass drawing into `format`: no depth (ADR-0027), but
         * a world quad's tests its view's reversed-Z depth without writing it, as transparent items do.
         */
        void SetTargetState(PipelineDesc& desc, TextureFormat colour, std::uint32_t samples, bool world)
        {
            desc.depth.test = world;
            desc.depth.write = false;
            desc.depth.compare = CompareOp::GreaterEqual;
            if (world)
            {
                desc.depthFormat = TextureFormat::Depth32Float;
            }
            else
            {
                desc.depthFormat.reset();
            }
            desc.colourFormats = {colour};
            desc.samples = samples;
        }

        /** @brief `resources` sorted by id, each once. */
        void SortUnique(std::vector<GraphResource>& resources)
        {
            std::sort(resources.begin(), resources.end(), [](GraphResource a, GraphResource b) { return a.id < b.id; });
            resources.erase(std::unique(resources.begin(), resources.end()), resources.end());
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
        m_worldBlocks =
            std::make_unique<BlockRing>(m_device, m_targetLayout, false, sizeof(TargetBlock), "2D world target");
        m_vertices = std::make_unique<ByteRing>(m_device, BufferUsage::Vertex, "2D vertices");
        m_indices = std::make_unique<ByteRing>(m_device, BufferUsage::Index, "2D indices");
        m_parameters = std::make_unique<ByteRing>(m_device, BufferUsage::Uniform, "2D parameters");

        // builtin:2d with no keys: linear vertex colours, which carry the opacity, times the premultiplied texel; with
        // BLUR, one pass of a Gaussian; with SHADOW too, the blurred alpha tinted.
        m_composite.container = std::make_unique<ShaderContainer>(ReadBuiltin(BuiltinProgram::Draw2D, m_logger));
        for (std::size_t kind = 0; kind < m_composite.variants.size(); ++kind)
        {
            Composite::Variant& variant = m_composite.variants.at(kind);
            variant.variant = m_composite.container->FindVariant(compositeKeys.at(kind));
            if (variant.variant == nullptr)
            {
                continue;
            }
            BindingGroupLayoutDesc layout;
            for (const ShaderBinding& binding : variant.variant->bindings)
            {
                if (binding.set == CommandSet)
                {
                    layout.entries.push_back({binding.binding, binding.kind});
                }
            }
            variant.layout = m_device.CreateBindingGroupLayout(layout, "2D composite");
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
        for (const Composite::Variant& variant : m_composite.variants)
        {
            m_device.Destroy(variant.layout);
        }
        m_targetBlocks.reset();
        m_worldBlocks.reset();
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
                                                                const PassFormat& format, bool masked)
    {
        const auto cached = std::find_if(m_pipelines.begin(), m_pipelines.end(), [&](const PipelineEntry& entry) {
            return entry.format == format && entry.masked == masked && entry.description == description;
        });
        if (cached != m_pipelines.end())
        {
            return cached->pipeline.IsEmpty() ? nullptr : &*cached;
        }

        const ShaderEntry& shader = m_shaders.at(KeyOf(description.shader));
        bool gamma = description.blendSpace == BlendSpace2D::Gamma;
        if (gamma && format.format != TextureFormat::Rgba8Srgb)
        {
            m_logger.Error("FenrirRenderer: a 2D pipeline description with shader {} asks for the Gamma blend space, "
                           "which needs an 8-bit sRGB target, but its target is {}; it draws as Linear",
                           description.shader.index, formatNames.at(static_cast<std::size_t>(format.format)));
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
        const ShaderVariant* variant = nullptr;
        if (masked)
        {
            std::vector<std::string_view> maskedKeys = keys;
            maskedKeys.emplace_back("MASK");
            // FindVariant passes over keys a shader does not have, so a shader without MASK is told apart first.
            const std::vector<std::string>& declared = shader.container->keys;
            if (std::ranges::find(declared, std::string_view("MASK")) != declared.end())
            {
                variant = shader.container->FindVariant(maskedKeys);
            }
            if (variant == nullptr)
            {
                // The unmasked pipeline draws them.
                LogOnce(std::format("no mask {}", shader.container->name),
                        std::format("2D shader '{}' has no MASK variant, so the commands a mask group reaches draw "
                                    "unmasked",
                                    shader.container->name));
                return Pipeline(description, format, false);
            }
        }
        else
        {
            variant = shader.container->FindVariant(keys);
        }

        PipelineEntry& entry = m_pipelines.emplace_back();
        entry.description = description;
        entry.format = format;
        entry.masked = masked;
        entry.variant = variant;
        if (entry.variant == nullptr)
        {
            return nullptr;
        }
        entry.layout = shader.layouts.at(entry.variant);

        PipelineDesc desc;
        desc.shader = entry.variant;
        desc.sets = {m_targetLayout, entry.layout, BindingGroupLayoutHandle{}, BindingGroupLayoutHandle{}};
        desc.vertexBuffers = {DeviceLayout(description.vertexLayout)};
        // Every 2D target's rules: no depth and no culling (ADR-0027); a world quad's tests the view's depth.
        desc.cull = CullMode::None;
        SetTargetState(desc, format.format, format.samples, format.world);
        desc.blend = deviceBlends.at(static_cast<std::size_t>(description.blendMode));
        desc.encodeSrgb = !gamma;
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

    PipelineHandle Draw2DFeature::CompositePipeline(CompositeKind kind, Graphics::BlendMode blend,
                                                    const PassFormat& format)
    {
        const auto key = std::make_tuple(kind, blend, format);
        if (const auto found = m_composite.pipelines.find(key); found != m_composite.pipelines.end())
        {
            return found->second;
        }
        PipelineHandle& pipeline = m_composite.pipelines[key];
        const Composite::Variant& variant = m_composite.variants.at(static_cast<std::size_t>(kind));
        if (variant.variant == nullptr)
        {
            return pipeline;
        }
        PipelineDesc desc;
        desc.shader = variant.variant;
        desc.sets = {m_targetLayout, variant.layout, BindingGroupLayoutHandle{}, BindingGroupLayoutHandle{}};
        desc.vertexBuffers = {DeviceLayout(Vertex2DLayout())};
        desc.cull = CullMode::None;
        SetTargetState(desc, format.format, format.samples, format.world);
        desc.blend = blend;
        desc.encodeSrgb = true;
        const PipelineResult made = m_device.CreatePipeline(desc, "2D composite");
        pipeline = made.pipeline;
        if (pipeline.IsEmpty())
        {
            m_logger.Error("FenrirRenderer: the 2D composite pipeline could not be made: {}", made.diagnostic);
        }
        return pipeline;
    }

    bool Draw2DFeature::Resolve(const DrawList2D& list, const DrawCommand2D& command, std::size_t vertexOffset,
                                std::size_t indexOffset, const ListTarget& target, const MaskState* mask,
                                TargetPass& pass)
    {
        if (mask != nullptr && mask->kind == MaskState::Kind::Nothing)
        {
            // A mask covering nothing masks everything away.
            return false;
        }
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
        // A world quad has no hardware scissor: its view's viewport is set as it is recorded, and the painter's shader
        // realises the clips.
        draw.scissor = command.scissor && !pass.world
                           ? ScissorPixels(*command.scissor, pass.scale, pass.originX, pass.originY, pass.pixels)
                           : PixelRect{0, 0, pass.pixels.width, pass.pixels.height};
        if (draw.scissor.width == 0 || draw.scissor.height == 0)
        {
            return false;
        }
        const bool masked = mask != nullptr && mask->kind == MaskState::Kind::Image;
        const PipelineEntry* pipeline = Pipeline(description, FormatOf(pass), masked);
        if (pipeline == nullptr)
        {
            return false;
        }

        // Set 1: the parameter block, then the textures, slot by slot, in binding order; a masked variant's `mask`
        // texture is the mask's image, bound as the pass is recorded, and takes no slot.
        const ShaderVariant& variant = *pipeline->variant;
        if (!pipeline->layout.IsEmpty())
        {
            BindingGroupDesc group{pipeline->layout, {}};
            std::size_t slot = 0;
            bool deferred = false;
            for (const ShaderBinding& binding : variant.bindings)
            {
                if (binding.set != CommandSet)
                {
                    continue;
                }
                BindingGroupEntry entry;
                entry.binding = binding.binding;
                if (binding.kind == ShaderBindingKind::Texture && pipeline->masked && binding.name == "mask")
                {
                    draw.mask = mask->image;
                    draw.maskEntry = group.entries.size();
                    entry.sampler = m_images.SamplerFor(LinearClamp2D);
                    pass.samples.push_back(mask->image);
                    deferred = true;
                }
                else if (binding.kind == ShaderBindingKind::Texture)
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
                    if (pipeline->masked)
                    {
                        // The mask's rectangle, where the masked variant reads it.
                        const auto member =
                            std::find_if(variant.materialMembers.begin(), variant.materialMembers.end(),
                                         [](const ShaderBlockMember& m) { return m.name == "maskRect"; });
                        if (member != variant.materialMembers.end() &&
                            member->offset + sizeof(Math::Vec4) <= block.size())
                        {
                            std::memcpy(block.data() + member->offset, &mask->rect, sizeof(Math::Vec4));
                        }
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
            m_deferredGroups.push_back(deferred);
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

    void Draw2DFeature::AddImageQuad(TargetPass& into, const ImageQuad& quad, GraphResource source, CompositeKind kind,
                                     Graphics::BlendMode blend, const BlurBlock* block)
    {
        const PipelineHandle pipeline = CompositePipeline(kind, blend, FormatOf(into));
        if (pipeline.IsEmpty())
        {
            return;
        }
        const Math::Vec4& uv = quad.uv;
        const std::array<Vertex2D, 4> corners{{
            {{quad.left, quad.top}, {uv.x, uv.y}, quad.colour},
            {{quad.right, quad.top}, {uv.z, uv.y}, quad.colour},
            {{quad.right, quad.bottom}, {uv.z, uv.w}, quad.colour},
            {{quad.left, quad.bottom}, {uv.x, uv.w}, quad.colour},
        }};
        constexpr std::array<std::uint16_t, 6> indices{0, 2, 1, 0, 3, 2};

        Draw draw;
        draw.pipeline = pipeline;
        draw.composite = source;
        draw.compositeKind = kind;
        if (block != nullptr)
        {
            // The variant's block, its defaults with the blur's values over them.
            const ShaderVariant& variant = *m_composite.variants.at(static_cast<std::size_t>(kind)).variant;
            std::vector<std::byte> bytes = variant.defaultBlock;
            bytes.resize(std::max(bytes.size(), sizeof(BlurBlock)));
            std::memcpy(bytes.data(), block, sizeof(BlurBlock));
            draw.compositeParameters = m_parameterBytes.size();
            m_parameterBytes.insert(m_parameterBytes.end(), bytes.begin(), bytes.end());
            m_parameterBytes.resize(AlignUp(m_parameterBytes.size(), UniformBlockAlignment));
        }
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
        into.samples.push_back(source);
    }

    Draw2DFeature::ImageQuad Draw2DFeature::QuadOver(const TargetPass& group, const TargetPass& into, Math::Vec2 offset)
    {
        // The group's pixels, in the list's local units: the quad covers its image exactly.
        const float scale = into.scale;
        ImageQuad quad;
        quad.left = static_cast<float>(group.originX) / scale + offset.x;
        quad.top = static_cast<float>(group.originY) / scale + offset.y;
        quad.right =
            static_cast<float>(group.originX + static_cast<std::int64_t>(group.pixels.width)) / scale + offset.x;
        quad.bottom =
            static_cast<float>(group.originY + static_cast<std::int64_t>(group.pixels.height)) / scale + offset.y;
        return quad;
    }

    GraphResource Draw2DFeature::TransientImage(RenderGraph& graph, const char* name, PixelSize pixels,
                                                TextureFormat format)
    {
        TextureDesc desc;
        desc.format = format;
        desc.width = pixels.width;
        desc.height = pixels.height;
        desc.usage = TextureUsage::ColourAttachment | TextureUsage::Sampled;
        m_stats.transientImageBytes += pixels.width * pixels.height * GroupPixelBytes;
        return graph.CreateTexture(name, desc);
    }

    namespace
    {
        /**
         * @brief A pass of a Gaussian of `sigma` texels along `step` (the UV of one texel), with at most MaxBlurTaps2D
         * taps a side spaced to reach three sigmas.
         */
        Math::Vec4 GaussianAlong(Math::Vec2 step, float sigma)
        {
            const float reach = std::ceil(3.0f * std::max(sigma, 0.0f));
            const float taps = std::min(reach, static_cast<float>(MaxBlurTaps2D));
            const float spacing = taps > 0.0f ? reach / taps : 1.0f;
            return {step.x * spacing, step.y * spacing, sigma > 0.0f ? spacing / sigma : 0.0f, taps};
        }
    } // namespace

    Draw2DFeature::Blurred Draw2DFeature::BlurAcross(RenderGraph& graph, GraphResource source, PixelSize pixels,
                                                     Math::Vec4 bounds, float sigma, TextureFormat format, bool clamp)
    {
        // The region blurred, in the source's pixels, at half its size past the threshold.
        const float regionWidth = (bounds.z - bounds.x) * static_cast<float>(pixels.width);
        const float regionHeight = (bounds.w - bounds.y) * static_cast<float>(pixels.height);
        const bool half = sigma > HalfResolutionBlur2D;
        const auto size = [half](float length) {
            return std::max<std::uint32_t>(
                static_cast<std::uint32_t>(std::ceil(half ? length * 0.5f : std::round(length))), 1u);
        };
        Blurred blurred;
        blurred.pixels = {size(regionWidth), size(regionHeight)};
        blurred.image = TransientImage(graph, "2D blur", blurred.pixels, format);

        TargetPass pass;
        pass.name = "Blur 2D";
        pass.stage = m_listStage;
        pass.target = blurred.image;
        pass.load = LoadOp::DontCare;
        pass.format = format;
        pass.pixels = blurred.pixels;
        const float texels = static_cast<float>(blurred.pixels.width);
        BlurBlock block;
        block.blur = GaussianAlong({(bounds.z - bounds.x) / texels, 0.0f}, sigma * texels / regionWidth);
        block.bounds = bounds;
        block.options.x = clamp ? 1.0f : 0.0f;
        ImageQuad quad;
        quad.right = texels;
        quad.bottom = static_cast<float>(blurred.pixels.height);
        quad.uv = bounds;
        AddImageQuad(pass, quad, source, CompositeKind::Blur, Graphics::BlendMode::None, &block);
        m_passes.push_back(std::move(pass));
        m_finished.push_back(m_passes.size() - 1);
        return blurred;
    }

    Draw2DFeature::BlurBlock Draw2DFeature::BlurDown(const Blurred& blurred, std::uint32_t height, float sigma,
                                                     bool clamp)
    {
        const float texels = static_cast<float>(blurred.pixels.height);
        BlurBlock block;
        block.blur = GaussianAlong({0.0f, 1.0f / texels}, sigma * texels / static_cast<float>(std::max(height, 1u)));
        block.options.x = clamp ? 1.0f : 0.0f;
        return block;
    }

    std::size_t Draw2DFeature::AddBackdrop(RenderGraph& graph, std::size_t group, std::size_t into, float sigma)
    {
        // The group's bounds and three sigmas about them, in the root's pixels, inside the pass drawn into.
        const TargetPass& outer = m_passes[into];
        const TargetPass& panel = m_passes[group];
        const std::int64_t reach = BlurReach(sigma);
        const std::int64_t outerRight = outer.originX + static_cast<std::int64_t>(outer.pixels.width);
        const std::int64_t outerBottom = outer.originY + static_cast<std::int64_t>(outer.pixels.height);
        const std::int64_t left = std::max<std::int64_t>(panel.originX - reach, outer.originX);
        const std::int64_t top = std::max<std::int64_t>(panel.originY - reach, outer.originY);
        const std::int64_t right =
            std::min<std::int64_t>(panel.originX + static_cast<std::int64_t>(panel.pixels.width) + reach, outerRight);
        const std::int64_t bottom =
            std::min<std::int64_t>(panel.originY + static_cast<std::int64_t>(panel.pixels.height) + reach, outerBottom);
        const PixelSize copied{static_cast<std::uint32_t>(right - left), static_cast<std::uint32_t>(bottom - top)};

        // The pass so far ends here: a group's image is made now, the overlay's is drawn only if it draws something.
        if (m_passes[into].target.IsEmpty())
        {
            m_passes[into].target = TransientImage(graph, "2D group", m_passes[into].pixels, m_passes[into].format);
        }
        if (!m_passes[into].draws.empty() || m_passes[into].load == LoadOp::Clear)
        {
            m_finished.push_back(into);
        }
        TargetPass next;
        next.name = m_passes[into].name;
        next.stage = m_passes[into].stage;
        next.target = m_passes[into].target;
        next.load = LoadOp::Load;
        next.format = m_passes[into].format;
        next.pixels = m_passes[into].pixels;
        next.originX = m_passes[into].originX;
        next.originY = m_passes[into].originY;
        next.scale = m_passes[into].scale;

        // What it holds under them, copied, then blurred across and down.
        TargetPass copy;
        copy.name = "Backdrop 2D";
        copy.stage = m_listStage;
        copy.format = next.format;
        copy.pixels = copied;
        copy.target = TransientImage(graph, "2D backdrop", copied, next.format);
        copy.copy = Copy{next.target,
                         {static_cast<std::int32_t>(left - next.originX), static_cast<std::int32_t>(top - next.originY),
                          copied.width, copied.height}};
        const GraphResource backdrop = copy.target;
        m_passes.push_back(std::move(copy));
        m_finished.push_back(m_passes.size() - 1);
        const Blurred blurred = BlurAcross(graph, backdrop, copied, {0.0f, 0.0f, 1.0f, 1.0f}, sigma, next.format, true);

        // Drawn back over the group's bounds in place of what was there.
        const TargetPass& bounds = m_passes[group];
        ImageQuad quad = QuadOver(bounds, next);
        const auto fraction = [](std::int64_t value, std::int64_t from, std::uint32_t length) {
            return static_cast<float>(value - from) / static_cast<float>(length);
        };
        quad.uv = {fraction(bounds.originX, left, copied.width), fraction(bounds.originY, top, copied.height),
                   fraction(bounds.originX + static_cast<std::int64_t>(bounds.pixels.width), left, copied.width),
                   fraction(bounds.originY + static_cast<std::int64_t>(bounds.pixels.height), top, copied.height)};
        const BlurBlock block = BlurDown(blurred, copied.height, sigma, true);
        AddImageQuad(next, quad, blurred.image, CompositeKind::Blur, Graphics::BlendMode::None, &block);
        m_passes.push_back(std::move(next));
        return m_passes.size() - 1;
    }

    bool Draw2DFeature::DrawList(const DrawList2D& list, ListTarget& target, RenderGraph& graph)
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

        // Each group open, innermost last: realised into a pass of its own, drawn flat (too deep, or groups off),
        // culled (covering no pixel, or inside a culled group), or dropped (a mask's shape not drawn, what it reaches
        // left unmasked).
        enum class Open : std::uint8_t
        {
            Realised,
            Flat,
            Culled,
            Dropped,
        };
        std::vector<std::pair<Open, std::size_t>> open;
        std::vector<std::size_t> realised{target.root};
        std::vector<MaskState> masks;
        std::size_t culled = 0;
        const std::vector<std::size_t> ends = GroupEnds(list);
        // Moves the innermost realised pass on to `next`, the pass that goes on drawing into its target.
        const auto moveOn = [&](std::size_t next) {
            const std::size_t from = realised.back();
            realised.back() = next;
            for (auto& [kind, index] : open)
            {
                if (kind == Open::Realised && index == from)
                {
                    index = next;
                }
            }
            if (realised.size() == 1)
            {
                target.root = next;
            }
        };
        for (std::size_t c = 0; c < list.commands.size(); ++c)
        {
            const Command2D& command = list.commands[c];
            if (const auto* draw = std::get_if<DrawCommand2D>(&command))
            {
                if (culled == 0)
                {
                    Resolve(list, *draw, vertexOffset, indexOffset, target, masks.empty() ? nullptr : &masks.back(),
                            m_passes[realised.back()]);
                }
            }
            else if (const auto* begin = std::get_if<BeginGroup2D>(&command))
            {
                const auto& composite = std::get<EndGroup2D>(list.commands[ends[c]]);
                if (culled > 0)
                {
                    open.emplace_back(Open::Culled, 0);
                    ++culled;
                    continue;
                }
                const bool tooDeep = realised.size() > MaxGroupDepth2D;
                if (tooDeep)
                {
                    LogOnce("depth", std::format("a 2D draw list's offscreen groups nest deeper than {}, so the deeper "
                                                 "ones draw flat into the group around them",
                                                 MaxGroupDepth2D));
                }
                if ((tooDeep || !m_offscreenGroups) && composite.mask)
                {
                    open.emplace_back(Open::Dropped, 0);
                    ++culled;
                    continue;
                }
                if (tooDeep || !m_offscreenGroups)
                {
                    open.emplace_back(Open::Flat, 0);
                    continue;
                }
                // The group's bounds in the root's pixels, rounded outwards (a blur's or a shadow's reach about them)
                // and clamped to the pass around it.
                const TargetPass& outer = m_passes[realised.back()];
                const bool spreads = !composite.mask && (composite.filter == GroupFilter2D::Blur ||
                                                         composite.filter == GroupFilter2D::DropShadow);
                const std::int64_t reach = spreads ? BlurReach(composite.radius * outer.scale) : 0;
                const PixelEdges edges = RoundOut(begin->bounds, outer.scale);
                const std::int64_t left = std::max<std::int64_t>(edges.left - reach, outer.originX);
                const std::int64_t top = std::max<std::int64_t>(edges.top - reach, outer.originY);
                const std::int64_t right =
                    std::min<std::int64_t>(edges.right + reach, outer.originX + outer.pixels.width);
                const std::int64_t bottom =
                    std::min<std::int64_t>(edges.bottom + reach, outer.originY + outer.pixels.height);
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
                group.stage = m_listStage;
                m_passes.push_back(std::move(group));
                open.emplace_back(Open::Realised, m_passes.size() - 1);
                realised.push_back(m_passes.size() - 1);
            }
            else if (const auto* end = std::get_if<EndGroup2D>(&command))
            {
                const auto [kind, index] = open.back();
                open.pop_back();
                if (kind == Open::Culled || kind == Open::Dropped)
                {
                    --culled;
                    if (end->mask && culled == 0)
                    {
                        // A mask covering no pixel masks everything away; one dropped masks nothing.
                        masks.push_back({kind == Open::Dropped ? MaskState::Kind::Unmasked : MaskState::Kind::Nothing});
                    }
                    continue;
                }
                if (kind == Open::Flat)
                {
                    continue;
                }
                realised.pop_back();
                const bool drawn = !m_passes[index].draws.empty() || !m_passes[index].target.IsEmpty();
                const float scale = m_passes[index].scale;
                const float sigma = end->radius * scale;
                const bool backdrop = !end->mask && end->filter == GroupFilter2D::BackdropBlur && sigma > 0.0f;
                if (drawn)
                {
                    if (m_passes[index].target.IsEmpty())
                    {
                        m_passes[index].target =
                            TransientImage(graph, "2D group", m_passes[index].pixels, m_passes[index].format);
                    }
                    m_finished.push_back(index);
                    ++m_stats.groups;
                }
                if (end->mask)
                {
                    if (!drawn)
                    {
                        masks.push_back({MaskState::Kind::Nothing});
                        continue;
                    }
                    const TargetPass& shape = m_passes[index];
                    masks.push_back(
                        {MaskState::Kind::Image,
                         shape.target,
                         {static_cast<float>(shape.originX) / scale, static_cast<float>(shape.originY) / scale,
                          scale / static_cast<float>(shape.pixels.width),
                          scale / static_cast<float>(shape.pixels.height)}});
                    continue;
                }
                if (backdrop && m_passes[realised.back()].world)
                {
                    // A world quad's lists draw into the view's scene colour as they are recorded: there is no image
                    // of what is under them to copy.
                    LogOnce("world backdrop", "a backdrop filter on a world-space 2D target's quad has no target to "
                                              "read, so its group draws without one");
                }
                else if (backdrop)
                {
                    moveOn(AddBackdrop(graph, index, realised.back(), sigma));
                }
                if (!drawn)
                {
                    continue;
                }

                // Blurs add passes, so no reference into m_passes is held across them.
                const Graphics::BlendMode blend = end->blend == GroupBlend2D::Additive
                                                      ? Graphics::BlendMode::Additive
                                                      : Graphics::BlendMode::Premultiplied;
                // The opacity scales the premultiplied texel's colour and alpha alike.
                const float opacity = std::clamp(end->opacity, 0.0f, 1.0f);
                ImageQuad quad = QuadOver(m_passes[index], m_passes[realised.back()]);
                quad.colour = PackLinear(Math::Vec4(opacity));
                const GraphResource image = m_passes[index].target;
                const PixelSize pixels = m_passes[index].pixels;
                if (end->filter == GroupFilter2D::Blur && sigma > 0.0f)
                {
                    const Blurred blurred = BlurAcross(graph, image, pixels, {0.0f, 0.0f, 1.0f, 1.0f}, sigma,
                                                       TextureFormat::Rgba8Srgb, false);
                    const BlurBlock block = BlurDown(blurred, pixels.height, sigma, false);
                    AddImageQuad(m_passes[realised.back()], quad, blurred.image, CompositeKind::Blur, blend, &block);
                    continue;
                }
                if (end->filter == GroupFilter2D::DropShadow)
                {
                    // The image's alpha, blurred across first when it blurs at all, then down as the shadow draws.
                    Blurred blurred{image, pixels};
                    if (sigma > 0.0f)
                    {
                        blurred = BlurAcross(graph, image, pixels, {0.0f, 0.0f, 1.0f, 1.0f}, sigma,
                                             TextureFormat::Rgba8Srgb, false);
                    }
                    BlurBlock block = BlurDown(blurred, pixels.height, sigma, false);
                    const Math::Vec4& tint = end->shadowColour;
                    const float alpha = std::clamp(tint.w, 0.0f, 1.0f) * opacity;
                    block.shadow = {tint.x * alpha, tint.y * alpha, tint.z * alpha, alpha};
                    TargetPass& under = m_passes[realised.back()];
                    ImageQuad shadow = QuadOver(m_passes[index], under, end->offset);
                    AddImageQuad(under, shadow, blurred.image, CompositeKind::Shadow, blend, &block);
                }
                AddImageQuad(m_passes[realised.back()], quad, image, CompositeKind::Plain, blend);
            }
            else if (std::holds_alternative<EndMask2D>(command))
            {
                if (culled == 0)
                {
                    masks.pop_back();
                }
            }
        }
        return true;
    }

    void Draw2DFeature::BeginFrame(std::uint64_t frameIndex)
    {
        if (m_begun == frameIndex)
        {
            return;
        }
        m_begun = frameIndex;
        for (const BindingGroupHandle group : m_frameGroups)
        {
            m_device.Destroy(group);
        }
        m_frameGroups.clear();
        m_stats = {};
        m_drawn = std::move(m_frame);
        m_frame = {};
        m_passes.clear();
        m_finished.clear();
        m_worlds.clear();
        m_vertexBytes.clear();
        m_indexBytes.clear();
        m_parameterBytes.clear();
        m_parameterEntries.clear();
        m_pendingGroups.clear();
        m_deferredGroups.clear();
        m_vertexBuffer = {};
        m_indexBuffer = {};
        m_parameterBuffer = {};
        m_passesAdded = false;
    }

    std::vector<WorldQuads::Quad> Draw2DFeature::ResolveWorldQuads(RenderGraph& graph, const FrameContext& frame,
                                                                   TextureFormat colour, std::uint32_t samples,
                                                                   std::size_t views)
    {
        // The scene renderer is added before this feature (Renderer::Install), so its passes, and this call, come
        // before AddPasses, which writes the bytes and groups the quads' draws record with.
        BeginFrame(frame.frameIndex);
        if (m_passesAdded)
        {
            LogOnce("world order", "the world-space 2D target's quads were resolved after the 2D feature added its "
                                   "passes, so they draw nothing");
            return {};
        }
        std::vector<Quad> quads;
        // Its lists sample image 2D targets as they were: those are drawn after the views.
        static const std::map<AssetKey, GraphResource> earlierImages;
        m_listStage = RenderStage::Transparent;
        for (const WorldSubmission2D& world : m_drawn.worlds)
        {
            TargetPass root;
            root.name = "World 2D";
            root.stage = RenderStage::Transparent;
            root.world = true;
            root.format = colour;
            root.sampleCount = samples;
            root.pixels = {static_cast<std::uint32_t>(std::ceil(std::max(world.logicalSize.x, 0.0f))),
                           static_cast<std::uint32_t>(std::ceil(std::max(world.logicalSize.y, 0.0f)))};
            if (root.pixels.width == 0 || root.pixels.height == 0)
            {
                continue;
            }
            m_passes.push_back(std::move(root));
            ListTarget target{m_passes.size() - 1, AssetKey{}, &earlierImages};
            for (const DrawList2D& list : world.lists)
            {
                DrawList(list, target, graph);
            }
            if (m_passes[target.root].draws.empty())
            {
                continue;
            }
            m_worlds.push_back({target.root, world.localToWorld, world.logicalSize});
            Quad& quad = quads.emplace_back();
            quad.origin = world.origin;
            const Math::Vec2 size = world.logicalSize;
            const std::array<Math::Vec2, 4> corners{{{0.0f, 0.0f}, {size.x, 0.0f}, {size.x, size.y}, {0.0f, size.y}}};
            for (std::size_t c = 0; c < corners.size(); ++c)
            {
                quad.corners.at(c) = Math::Vec3(world.localToWorld * Math::Vec4(corners.at(c), 0.0f, 1.0f));
            }
        }
        m_listStage = RenderStage::Overlay;
        m_worldBlocks->Reserve(std::max<std::size_t>(m_worlds.size() * views, 1));
        return quads;
    }

    std::vector<GraphResource> Draw2DFeature::WorldQuadSamples(std::size_t quad) const
    {
        std::vector<GraphResource> samples = m_passes.at(m_worlds.at(quad).pass).samples;
        SortUnique(samples);
        return samples;
    }

    BindingGroupHandle Draw2DFeature::WorldQuadTarget(std::uint64_t frameIndex, std::size_t quad, std::size_t view,
                                                      const Math::Mat4& viewProjection)
    {
        // The list's local units through the quad's world matrix and the view, at a content scale of 1 over the
        // quad's logical size.
        const WorldQuad& world = m_worlds.at(quad);
        const TargetBlock block{viewProjection * world.localToWorld,
                                Math::Vec4(world.logicalSize.x, world.logicalSize.y, 0.0f, 0.0f),
                                Math::Vec4(1.0f, 0.0f, 0.0f, 0.0f)};
        const std::size_t slot = view * m_worlds.size() + quad;
        m_worldBlocks->Write(frameIndex, slot, std::as_bytes(std::span(&block, 1)));
        return m_worldBlocks->Group(frameIndex, slot);
    }

    void Draw2DFeature::RecordWorldQuad(PassContext& context, std::size_t quad, BindingGroupHandle target,
                                        PixelRect viewport)
    {
        context.Encoder().SetViewport(viewport);
        RecordDraws(context, m_passes.at(m_worlds.at(quad).pass).draws, target, viewport);
    }

    void Draw2DFeature::RecordDraws(PassContext& context, const std::vector<Draw>& draws, BindingGroupHandle target,
                                    const std::optional<PixelRect>& scissor)
    {
        PassEncoder& encoder = context.Encoder();
        for (const Draw& draw : draws)
        {
            BindingGroupHandle group = draw.group;
            if (!draw.composite.IsEmpty())
            {
                group = CompositeGroup(draw.compositeKind, context.Texture(draw.composite), m_parameterBuffer,
                                       draw.compositeParameters);
            }
            else if (!draw.mask.IsEmpty())
            {
                BindingGroupDesc desc = draw.maskedGroup;
                desc.entries.at(draw.maskEntry).texture = context.Texture(draw.mask);
                group = m_device.CreateBindingGroup(desc, "2D command");
                m_frameGroups.push_back(group);
            }
            encoder.SetPipeline(draw.pipeline);
            encoder.SetBindingGroup(0, target);
            if (!group.IsEmpty())
            {
                encoder.SetBindingGroup(1, group);
            }
            encoder.SetVertexBuffer(0, m_vertexBuffer, draw.vertexOffset);
            encoder.SetIndexBuffer(m_indexBuffer, draw.indexFormat, draw.indexOffset);
            encoder.SetScissor(scissor.value_or(draw.scissor));
            encoder.DrawIndexed(
                {.indexCount = draw.indexCount, .firstIndex = draw.firstIndex, .baseVertex = draw.baseVertex});
        }
    }

    void Draw2DFeature::AddPasses(RenderGraph& graph, const FrameContext& frame)
    {
        BeginFrame(frame.frameIndex);
        m_passesAdded = true;
        const Frame2D& drawn = m_drawn;

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
            ListTarget target{m_passes.size() - 1, image.image, &images};
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
            ListTarget target{m_passes.size() - 1, AssetKey{}, &images};

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
        if (m_finished.empty() && m_worlds.empty())
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
            m_parameterBuffer = m_parameters->Current(frameIndex);
            for (const auto& [group, entry] : m_parameterEntries)
            {
                m_pendingGroups.at(group).entries.at(entry).buffer = m_parameterBuffer;
            }
        }
        std::vector<BindingGroupHandle> groups;
        for (std::size_t i = 0; i < m_pendingGroups.size(); ++i)
        {
            // A masked draw's group binds the mask's image, which exists only as its pass is recorded.
            groups.push_back(m_deferredGroups[i] ? BindingGroupHandle{}
                                                 : m_device.CreateBindingGroup(m_pendingGroups[i], "2D command"));
            if (!groups.back().IsEmpty())
            {
                m_frameGroups.push_back(groups.back());
            }
        }
        const auto bindGroups = [&](TargetPass& pass) {
            for (Draw& draw : pass.draws)
            {
                if (draw.groupIndex == 0)
                {
                    continue;
                }
                if (draw.mask.IsEmpty())
                {
                    draw.group = groups.at(draw.groupIndex - 1);
                }
                else
                {
                    draw.maskedGroup = m_pendingGroups.at(draw.groupIndex - 1);
                }
            }
        };
        // The world quads' draws stay here, for the views' Transparent passes to record.
        for (const WorldQuad& world : m_worlds)
        {
            bindGroups(m_passes[world.pass]);
        }

        m_targetBlocks->Reserve(m_finished.size());
        m_vertexBuffer = m_vertices->Current(frameIndex);
        m_indexBuffer = m_indices->Current(frameIndex);
        for (std::size_t slot = 0; slot < m_finished.size(); ++slot)
        {
            TargetPass& pass = m_passes[m_finished[slot]];
            if (pass.copy)
            {
                // A backdrop's copy: what the target holds under it, into the copy's image.
                graph.AddPass(
                    pass.name, pass.stage, QueueKind::Copy,
                    [source = pass.copy->source, target = pass.target](PassBuilder& builder) {
                        builder.CopySource(source);
                        builder.CopyDestination(target);
                    },
                    [source = pass.copy->source, region = pass.copy->region,
                     target = pass.target](PassContext& context) {
                        context.Encoder().CopyTexture({context.Texture(source), region, context.Texture(target), 0, 0});
                    });
                continue;
            }
            const TargetBlock block = BlockOf(pass.pixels, pass.scale, pass.originX, pass.originY);
            m_targetBlocks->Write(frameIndex, slot, std::as_bytes(std::span(&block, 1)));
            bindGroups(pass);
            SortUnique(pass.samples);

            const BindingGroupHandle targetGroup = m_targetBlocks->Group(frameIndex, slot);
            const PixelRect viewport{0, 0, pass.pixels.width, pass.pixels.height};
            // The overlay's passes are in the Overlay stage, after each view's Output and before the debug feature's
            // Overlay pass, an image's and a group's before what samples them, as the graph orders them; a world
            // quad's groups are in the Transparent stage, before the views' Transparent passes that composite them.
            graph.AddPass(
                pass.name, pass.stage, QueueKind::Graphics,
                [target = pass.target, load = pass.load, samples = pass.samples](PassBuilder& builder) {
                    builder.ColourAttachment(target, load, {0.0f, 0.0f, 0.0f, 0.0f});
                    for (const GraphResource sampled : samples)
                    {
                        builder.Sample(sampled);
                    }
                },
                [this, recorded = std::move(pass.draws), viewport, targetGroup](PassContext& context) {
                    context.Encoder().SetViewport(viewport);
                    RecordDraws(context, recorded, targetGroup, std::nullopt);
                });
        }
    }

    BindingGroupHandle Draw2DFeature::CompositeGroup(CompositeKind kind, TextureHandle image, BufferHandle buffer,
                                                     std::size_t parameters)
    {
        const Composite::Variant& variant = m_composite.variants.at(static_cast<std::size_t>(kind));
        BindingGroupDesc desc{variant.layout, {}};
        for (const ShaderBinding& binding : variant.variant->bindings)
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
            else if (variant.variant->materialBlockBinding == binding.binding)
            {
                entry.buffer = buffer;
                entry.offset = parameters;
                entry.size = std::max<std::size_t>(binding.size, sizeof(BlurBlock));
            }
            desc.entries.push_back(entry);
        }
        const BindingGroupHandle group = m_device.CreateBindingGroup(desc, "2D composite");
        m_frameGroups.push_back(group);
        return group;
    }
} // namespace Fenrir::Render
