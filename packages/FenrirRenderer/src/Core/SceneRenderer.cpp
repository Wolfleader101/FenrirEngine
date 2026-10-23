#include "Core/SceneRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <string_view>
#include <type_traits>
#include <variant>

#include "Core/BuiltinShaders.hpp"
#include "FenrirLogger/ILogger.hpp"

namespace Fenrir::Render
{
    namespace
    {
        using namespace Fenrir::Graphics;

        /** @brief The frame block, as `Fenrir/Frame.glsl` declares it (std140). */
        struct FrameBlock
        {
            Math::Vec4 lightDirection;
            Math::Vec4 lightColour;
            Math::Vec4 ambient;
            Math::Vec4 time;
        };
        static_assert(sizeof(FrameBlock) == 64);

        /** @brief The view block, as `Fenrir/View.glsl` declares it (std140). */
        struct ViewBlock
        {
            Math::Mat4 view;
            Math::Mat4 projection;
            Math::Mat4 viewProjection;
            Math::Mat4 inverseViewProjection;
            Math::Vec4 cameraPosition;
            Math::Vec4 viewport;
        };
        static_assert(sizeof(ViewBlock) == 288);

        /** @brief One draw record, as `Fenrir/Draw.glsl` declares it (std430). */
        struct DrawBlock
        {
            Math::Mat4 world;
            Math::Mat4 normal;
        };
        static_assert(sizeof(DrawBlock) == 128);

        /** @brief The background block, as `builtin:background` declares it (std140). */
        struct BackgroundBlock
        {
            Math::Vec4 colour;
        };

        /** @brief One binding of a fixed set as the renderer declares it: a block's kind and size (0: any size). */
        struct FixedBinding
        {
            std::uint32_t set = 0;
            std::uint32_t binding = 0;
            ShaderBindingKind kind = ShaderBindingKind::UniformBuffer;
            std::uint32_t size = 0;
        };

        // Sets 0, 1 and 3, declared in C++ (ADR-0024). The draw buffer's size depends on its item count.
        [[maybe_unused]] constexpr std::array<FixedBinding, 3> FixedBindings{{
            {0, 0, ShaderBindingKind::UniformBuffer, sizeof(FrameBlock)},
            {1, 0, ShaderBindingKind::UniformBuffer, sizeof(ViewBlock)},
            {3, 0, ShaderBindingKind::StorageBuffer, 0},
        }};

        /** @brief The binding set of the material. */
        constexpr std::uint32_t MaterialSet = 2;

        /** @brief Uniform ranges are bound at multiples of this, the largest alignment GL implementations ask for. */
        constexpr std::size_t UniformAlignment = 256;

        /** @brief One standard vertex stream (`Fenrir/Vertex.glsl`), by location: its name, format and size. */
        struct StreamFormat
        {
            std::string_view name;
            VertexFormat format = VertexFormat::Float3;
            std::size_t size = 0;
            /** @brief Where its constant default sits in the default stream buffer. */
            std::size_t defaultOffset = 0;
            std::string_view defaultName;
        };

        constexpr std::array<StreamFormat, 6> Streams{{
            {"positions", VertexFormat::Float3, 12, 0, "the origin"},
            {"normals", VertexFormat::Float3, 12, 12, "normal +Y"},
            {"tangents", VertexFormat::Float4, 16, 24, "tangent +X"},
            {"uv0", VertexFormat::Float2, 8, 40, "UV 0"},
            {"uv1", VertexFormat::Float2, 8, 48, "UV 0"},
            {"colours", VertexFormat::Float4, 16, 56, "white"},
        }};

        // The constants above, back to back: position, normal, tangent, UV0, UV1, colour.
        constexpr std::array<float, 18> DefaultStreams{0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                                       1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f};

        constexpr std::uint8_t OpaqueStage = static_cast<std::uint8_t>(RenderStage::Opaque);

        template <typename T>
        std::span<const std::byte> BytesOf(const T& value)
        {
            return std::as_bytes(std::span(&value, 1));
        }

        std::size_t RoundUp(std::size_t size, std::size_t multiple)
        {
            return (size + multiple - 1) / multiple * multiple;
        }

        /** @brief The standard vertex locations a variant reads, as bits. */
        std::uint32_t ReadMask(const ShaderVariant& variant)
        {
            std::uint32_t mask = 0;
            for (const ShaderVertexInput& input : variant.vertexInputs)
            {
                if (input.location < Streams.size())
                {
                    mask |= 1u << input.location;
                }
            }
            return mask;
        }

        [[maybe_unused]] std::string_view KindName(ShaderBindingKind kind)
        {
            // By ShaderBindingKind: uniform block, storage block, texture.
            constexpr std::array<std::string_view, 3> names{"uniform block", "storage block", "texture"};
            return names.at(static_cast<std::size_t>(kind));
        }

        /**
         * @brief Why a variant's sets 0, 1 and 3 disagree with what the renderer binds there, or empty. Checked in
         * debug builds only (ADR-0024).
         */
        std::string CheckFixedSets([[maybe_unused]] const ShaderVariant& variant)
        {
#ifndef NDEBUG
            for (const ShaderBinding& binding : variant.bindings)
            {
                if (binding.set == MaterialSet)
                {
                    continue;
                }
                const auto declared = std::find_if(FixedBindings.begin(), FixedBindings.end(), [&](const auto& fixed) {
                    return fixed.set == binding.set && fixed.binding == binding.binding;
                });
                if (declared == FixedBindings.end())
                {
                    return std::format("set {} binding {} ('{}') is not a binding the renderer declares", binding.set,
                                       binding.binding, binding.name);
                }
                if (declared->kind != binding.kind)
                {
                    return std::format("set {} binding {} ('{}') is a {} in the shader, but the renderer declares a {}",
                                       binding.set, binding.binding, binding.name, KindName(binding.kind),
                                       KindName(declared->kind));
                }
                if (declared->size != 0 && declared->size != binding.size)
                {
                    return std::format("set {} binding {} ('{}') is {} bytes in the shader, but the renderer declares "
                                       "{}",
                                       binding.set, binding.binding, binding.name, binding.size, declared->size);
                }
            }
#endif
            return {};
        }

        /** @brief The parameter default of the texture parameter `name`, white if the variant does not say. */
        DefaultTexture DefaultTextureOf(const ShaderVariant& variant, std::string_view name)
        {
            const auto found = std::find_if(variant.parameters.begin(), variant.parameters.end(), [&](const auto& p) {
                return p.type == ShaderParameterType::Texture && p.name == name;
            });
            return found == variant.parameters.end() ? DefaultTexture::White : found->texture;
        }

        /** @brief Writes `material`'s number overrides that the variant's block has, at their std140 offsets. */
        void WriteOverrides(const ShaderVariant& variant, const MaterialData& material, std::vector<std::byte>& block)
        {
            for (const auto& [name, value] : material.overrides)
            {
                const auto member = std::find_if(variant.materialMembers.begin(), variant.materialMembers.end(),
                                                 [&](const ShaderBlockMember& m) { return m.name == name; });
                if (member == variant.materialMembers.end())
                {
                    continue;
                }
                // An override of another type than the member is kept by the store and ignored here.
                std::visit(
                    [&](const auto& v) {
                        using V = std::decay_t<decltype(v)>;
                        std::size_t components = 0;
                        ShaderValueType type = ShaderValueType::Other;
                        if constexpr (std::is_same_v<V, float>)
                        {
                            components = 1;
                            type = ShaderValueType::Float;
                        }
                        else if constexpr (std::is_same_v<V, Math::Vec2>)
                        {
                            components = 2;
                            type = ShaderValueType::Vec2;
                        }
                        else if constexpr (std::is_same_v<V, Math::Vec3>)
                        {
                            components = 3;
                            type = ShaderValueType::Vec3;
                        }
                        else if constexpr (std::is_same_v<V, Math::Vec4>)
                        {
                            components = 4;
                            type = ShaderValueType::Vec4;
                        }
                        if (components != 0 && member->type == type &&
                            member->offset + components * sizeof(float) <= block.size())
                        {
                            std::memcpy(std::span(block).subspan(member->offset).data(), &v,
                                        components * sizeof(float));
                        }
                    },
                    value);
            }
        }

        ShaderContainer ReadBuiltin(BuiltinProgram program, ILogger& logger)
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

        const ShaderVariant* BaseVariant(const ShaderContainer& container)
        {
            return container.FindVariant({});
        }

        /** @brief The device sampler a material texture parameter's description asks for. */
        SamplerDesc ToSamplerDesc(const SamplerDescription& description)
        {
            // By SamplerWrap: repeat, mirrored repeat, clamp to edge.
            constexpr std::array<AddressMode, 3> wraps{AddressMode::Repeat, AddressMode::MirroredRepeat,
                                                       AddressMode::ClampToEdge};
            SamplerDesc desc;
            desc.addressU = wraps.at(static_cast<std::size_t>(description.wrapU));
            desc.addressV = wraps.at(static_cast<std::size_t>(description.wrapV));
            desc.addressW = desc.addressU;
            const bool nearest = description.filter == SamplerFilter::Nearest;
            desc.minFilter = nearest ? Filter::Nearest : Filter::Linear;
            desc.magFilter = desc.minFilter;
            desc.mipFilter = description.filter == SamplerFilter::Trilinear ? Filter::Linear : Filter::Nearest;
            desc.maxAnisotropy = static_cast<float>(std::max<std::uint32_t>(description.anisotropy, 1));
            return desc;
        }

        /** @brief Whether `from` waits, directly or through other views, on `to`, by each view's `waitsOn`. */
        bool Waits(const std::vector<std::vector<std::size_t>>& waitsOn, std::size_t from, std::size_t to)
        {
            std::vector<bool> seen(waitsOn.size(), false);
            std::vector<std::size_t> stack{from};
            while (!stack.empty())
            {
                const std::size_t view = stack.back();
                stack.pop_back();
                if (view == to)
                {
                    return true;
                }
                if (seen[view])
                {
                    continue;
                }
                seen[view] = true;
                stack.insert(stack.end(), waitsOn[view].begin(), waitsOn[view].end());
            }
            return false;
        }

        /** @brief How a log line names a view's target: an image by its asset index, or the window. */
        std::string TargetName(AssetKey target)
        {
            return target.IsEmpty() ? std::string("the window") : std::format("image {}", target.index);
        }
    } // namespace

    // ---- BlockRing ----

    SceneRenderer::BlockRing::BlockRing(GraphicsDevice& device, BindingGroupLayoutHandle layout, bool storage,
                                        std::size_t slotSize, std::string label)
        : m_device(device), m_layout(layout), m_storage(storage),
          m_slotSize(storage ? slotSize : RoundUp(slotSize, UniformAlignment)), m_label(std::move(label))
    {
    }

    SceneRenderer::BlockRing::~BlockRing()
    {
        DestroyGroups();
    }

    void SceneRenderer::BlockRing::DestroyGroups()
    {
        for (const std::vector<BindingGroupHandle>& copy : m_groups)
        {
            for (const BindingGroupHandle group : copy)
            {
                m_device.Destroy(group);
            }
        }
        m_groups.clear();
    }

    void SceneRenderer::BlockRing::Reserve(std::size_t slots)
    {
        slots = std::max<std::size_t>(slots, 1);
        if (m_ring != nullptr && slots <= m_slots)
        {
            return;
        }
        // Grow by doubling, so a scene that keeps growing remakes its buffers rarely.
        m_slots = std::max(slots, m_slots * 2);
        DestroyGroups();
        m_ring.reset();
        const BufferUsage usage = m_storage ? BufferUsage::Storage : BufferUsage::Uniform;
        m_ring = std::make_unique<FrameRingBuffer>(m_device, BufferDesc{m_slots * m_slotSize, usage}, m_label);
        for (std::size_t copy = 0; copy < m_ring->CopyCount(); ++copy)
        {
            const BufferHandle buffer = m_ring->Current(copy);
            std::vector<BindingGroupHandle>& groups = m_groups.emplace_back();
            const std::size_t groupCount = m_storage ? 1 : m_slots;
            for (std::size_t slot = 0; slot < groupCount; ++slot)
            {
                BindingGroupEntry entry;
                entry.binding = 0;
                entry.buffer = buffer;
                entry.offset = m_storage ? 0 : slot * m_slotSize;
                entry.size = m_storage ? 0 : m_slotSize;
                groups.push_back(m_device.CreateBindingGroup({m_layout, {entry}}, m_label));
            }
        }
    }

    void SceneRenderer::BlockRing::Write(std::uint64_t frameIndex, std::size_t slot, std::span<const std::byte> bytes)
    {
        m_ring->Write(frameIndex, slot * m_slotSize, bytes);
    }

    BindingGroupHandle SceneRenderer::BlockRing::Group(std::uint64_t frameIndex, std::size_t slot) const
    {
        const std::vector<BindingGroupHandle>& copy =
            m_groups.at(static_cast<std::size_t>(frameIndex % m_groups.size()));
        return copy.at(m_storage ? 0 : slot);
    }

    // ---- SceneRenderer ----

    SceneRenderer::SceneRenderer(GraphicsDevice& device, ILogger& logger, std::uint32_t samples)
        : m_device(device), m_logger(logger), m_samples(std::max<std::uint32_t>(samples, 1))
    {
        m_frameLayout = m_device.CreateBindingGroupLayout({{{0, ShaderBindingKind::UniformBuffer}}}, "Frame");
        m_viewLayout = m_device.CreateBindingGroupLayout({{{0, ShaderBindingKind::UniformBuffer}}}, "View");
        m_drawLayout = m_device.CreateBindingGroupLayout({{{0, ShaderBindingKind::StorageBuffer}}}, "Draw");

        // The default textures (ADR-0024): 1x1 white, black and the flat tangent-space normal, linear.
        constexpr std::array<std::array<std::uint8_t, 4>, 3> texels{{
            {255, 255, 255, 255},
            {0, 0, 0, 255},
            {128, 128, 255, 255},
        }};
        constexpr std::array<std::string_view, 3> names{"Default white", "Default black", "Default normal"};
        for (std::size_t i = 0; i < texels.size(); ++i)
        {
            TextureDesc desc;
            desc.usage = TextureUsage::Sampled | TextureUsage::CopyDestination;
            m_defaultTextures.at(i) = m_device.CreateTexture(desc, names.at(i));
            m_device.WriteTexture(m_defaultTextures.at(i), 0, 0, std::as_bytes(std::span(texels.at(i))));
        }
        // SamplerDescription's default: repeat, trilinear, anisotropy 8.
        SamplerDesc sampler;
        sampler.maxAnisotropy = 8.0f;
        m_defaultSampler = m_device.CreateSampler(sampler, "Default sampler");
        SamplerDesc clamp;
        clamp.addressU = AddressMode::ClampToEdge;
        clamp.addressV = AddressMode::ClampToEdge;
        clamp.addressW = AddressMode::ClampToEdge;
        m_outputSampler = m_device.CreateSampler(clamp, "Output sampler");
        m_defaultStreams = m_device.CreateBuffer({sizeof(DefaultStreams), BufferUsage::Vertex},
                                                 std::as_bytes(std::span(DefaultStreams)), "Default vertex streams");

        m_fallback.container = ReadBuiltin(BuiltinProgram::Fallback, m_logger);
        m_fallback.variant = BaseVariant(m_fallback.container);
        if (m_fallback.variant != nullptr)
        {
            m_fallback.materialLayout = MaterialLayoutOf(*m_fallback.variant);
        }
        m_fallback.id = 0;

        m_background = ReadBuiltin(BuiltinProgram::Background, m_logger);
        m_output = ReadBuiltin(BuiltinProgram::Output, m_logger);
        if (const ShaderVariant* variant = BaseVariant(m_background))
        {
            m_backgroundLayout = MaterialLayoutOf(*variant);
        }
        if (const ShaderVariant* variant = BaseVariant(m_output))
        {
            m_outputLayout = MaterialLayoutOf(*variant);
        }
        m_backgroundPipeline = MakeInternalPipeline(m_background, BaseVariant(m_background), m_backgroundLayout,
                                                    TextureFormat::Rgba16Float, true, m_samples);
        // With MSAA, Output reads every sample of SceneColor and averages them: the resolve.
        constexpr std::array<std::string_view, 1> msaa{"MSAA"};
        const ShaderVariant* output = m_samples > 1 ? m_output.FindVariant(msaa) : BaseVariant(m_output);
        m_outputPipeline = MakeInternalPipeline(m_output, output, m_outputLayout, TextureFormat::Rgba8Srgb, false, 1);

        m_frameBlocks = std::make_unique<BlockRing>(m_device, m_frameLayout, false, sizeof(FrameBlock), "Frame");
        m_viewBlocks = std::make_unique<BlockRing>(m_device, m_viewLayout, false, sizeof(ViewBlock), "View");
        m_backgroundBlocks =
            std::make_unique<BlockRing>(m_device, m_backgroundLayout, false, sizeof(BackgroundBlock), "Background");
        m_drawBlocks = std::make_unique<BlockRing>(m_device, m_drawLayout, true, sizeof(DrawBlock), "Draw");
        m_frameBlocks->Reserve(1);
    }

    SceneRenderer::~SceneRenderer()
    {
        for (auto& [key, material] : m_materials)
        {
            DestroyMaterial(material);
        }
        for (auto& [key, mesh] : m_meshes)
        {
            DestroyMesh(mesh);
        }
        for (auto& [key, target] : m_targets)
        {
            m_device.Destroy(target.texture);
        }
        for (auto& [desc, sampler] : m_samplers)
        {
            m_device.Destroy(sampler);
        }
        for (auto& [key, pipeline] : m_pipelines)
        {
            m_device.Destroy(pipeline.pipeline);
        }
        for (auto& [key, shader] : m_shaders)
        {
            m_device.Destroy(shader.materialLayout);
        }
        for (auto& [texture, group] : m_outputGroups)
        {
            m_device.Destroy(group);
        }
        m_frameBlocks.reset();
        m_viewBlocks.reset();
        m_backgroundBlocks.reset();
        m_drawBlocks.reset();
        m_device.Destroy(m_fallback.materialLayout);
        m_device.Destroy(m_backgroundPipeline);
        m_device.Destroy(m_outputPipeline);
        m_device.Destroy(m_backgroundLayout);
        m_device.Destroy(m_outputLayout);
        m_device.Destroy(m_frameLayout);
        m_device.Destroy(m_viewLayout);
        m_device.Destroy(m_drawLayout);
        for (const TextureHandle texture : m_defaultTextures)
        {
            m_device.Destroy(texture);
        }
        m_device.Destroy(m_defaultSampler);
        m_device.Destroy(m_outputSampler);
        m_device.Destroy(m_defaultStreams);
    }

    std::uint32_t SceneRenderer::NextId()
    {
        return m_nextId++;
    }

    BindingGroupLayoutHandle SceneRenderer::MaterialLayoutOf(const ShaderVariant& variant)
    {
        BindingGroupLayoutDesc desc;
        for (const ShaderBinding& binding : variant.bindings)
        {
            if (binding.set == MaterialSet)
            {
                desc.entries.push_back({binding.binding, binding.kind});
            }
        }
        if (desc.entries.empty())
        {
            return {};
        }
        return m_device.CreateBindingGroupLayout(desc, "Material");
    }

    PipelineHandle SceneRenderer::MakeInternalPipeline(const ShaderContainer& container, const ShaderVariant* variant,
                                                       BindingGroupLayoutHandle layout, TextureFormat colour,
                                                       bool depthTested, std::uint32_t samples)
    {
        PipelineDesc desc;
        desc.shader = variant;
        desc.samples = samples;
        desc.sets.at(MaterialSet) = layout;
        desc.cull = CullMode::None;
        desc.colourFormats = {colour};
        desc.depth.test = depthTested;
        desc.depth.write = false;
        if (!depthTested)
        {
            desc.depthFormat.reset();
        }
        const PipelineResult made = m_device.CreatePipeline(desc, container.name);
        if (made.pipeline.IsEmpty())
        {
            m_logger.Error("FenrirRenderer: the built-in pipeline '{}' could not be made: {}", container.name,
                           made.diagnostic);
        }
        return made.pipeline;
    }

    SamplerHandle SceneRenderer::SamplerFor(const SamplerDescription& description)
    {
        const auto found = std::find_if(m_samplers.begin(), m_samplers.end(),
                                        [&](const auto& made) { return made.first == description; });
        if (found != m_samplers.end())
        {
            return found->second;
        }
        const SamplerHandle sampler = m_device.CreateSampler(ToSamplerDesc(description), "Material sampler");
        m_samplers.emplace_back(description, sampler);
        return sampler;
    }

    // ---- Prepare ----

    bool SceneRenderer::IsTargetCurrent(AssetKey key, std::uint32_t revision) const
    {
        const auto found = m_targets.find(key);
        return found != m_targets.end() && found->second.revision == revision;
    }

    void SceneRenderer::PrepareTarget(AssetKey key, std::uint32_t revision, PixelSize size)
    {
        ReleaseTarget(key);
        TargetEntry& entry = m_targets[key];
        entry.revision = revision;
        entry.size = size;
        TextureDesc desc;
        desc.format = TextureFormat::Rgba8Srgb;
        desc.width = size.width;
        desc.height = size.height;
        desc.usage = TextureUsage::ColourAttachment | TextureUsage::Sampled;
        entry.texture = m_device.CreateTexture(desc, "Render target image");
    }

    void SceneRenderer::ReleaseTarget(AssetKey key)
    {
        const auto found = m_targets.find(key);
        if (found != m_targets.end())
        {
            m_device.Destroy(found->second.texture);
            m_targets.erase(found);
        }
    }

    std::vector<AssetKey> SceneRenderer::TargetKeys() const
    {
        std::vector<AssetKey> keys;
        for (const auto& [key, entry] : m_targets)
        {
            keys.push_back(key);
        }
        return keys;
    }

    bool SceneRenderer::IsShaderCurrent(AssetKey key, std::uint32_t revision) const
    {
        const auto found = m_shaders.find(key);
        return found != m_shaders.end() && found->second.revision == revision;
    }

    void SceneRenderer::PrepareShader(AssetKey key, std::uint32_t revision, const ShaderData& shader)
    {
        ReleaseShader(key);
        ShaderContainerResult read = ReadShaderContainer(shader.bytes);
        if (!read.Ok() || read.container->materialSet != MaterialSet)
        {
            m_logger.Error("FenrirRenderer: shader {} cannot draw meshes: {}", key.index,
                           read.Ok() ? "it is a 2D shader" : read.diagnostic);
        }
        // Kept even when unusable, so it is not read again until its revision changes; its materials draw as the
        // fallback.
        ShaderEntry& entry = m_shaders[key];
        entry.revision = revision;
        entry.id = NextId();
        if (read.Ok() && read.container->materialSet == MaterialSet)
        {
            entry.container = std::move(*read.container);
            entry.variant = BaseVariant(entry.container);
            if (entry.variant != nullptr)
            {
                entry.materialLayout = MaterialLayoutOf(*entry.variant);
            }
        }
    }

    void SceneRenderer::ReleaseShader(AssetKey key)
    {
        const auto found = m_shaders.find(key);
        if (found == m_shaders.end())
        {
            return;
        }
        const ShaderEntry& shader = found->second;
        // Its materials' groups use its layout: they are made again once the shader is.
        for (auto it = m_materials.begin(); it != m_materials.end();)
        {
            if (it->second.shader == &shader)
            {
                DestroyMaterial(it->second);
                it = m_materials.erase(it);
            }
            else
            {
                ++it;
            }
        }
        DestroyPipelinesOf(shader);
        m_device.Destroy(shader.materialLayout);
        m_shaders.erase(found);
    }

    void SceneRenderer::DestroyPipelinesOf(const ShaderEntry& shader)
    {
        for (auto it = m_pipelines.begin(); it != m_pipelines.end();)
        {
            if (it->first.shader == &shader)
            {
                m_device.Destroy(it->second.pipeline);
                it = m_pipelines.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    bool SceneRenderer::IsMeshCurrent(AssetKey key, std::uint32_t revision) const
    {
        const auto found = m_meshes.find(key);
        return found != m_meshes.end() && found->second.revision == revision;
    }

    void SceneRenderer::PrepareMesh(AssetKey key, std::uint32_t revision, const MeshData& mesh)
    {
        ReleaseMesh(key);
        MeshEntry& entry = m_meshes[key];
        entry.revision = revision;
        entry.id = NextId();

        // Each stream the mesh has, back to back in one buffer.
        std::vector<std::byte> vertices;
        const auto append = [&](std::uint32_t location, const auto& stream) {
            if (stream.empty())
            {
                return;
            }
            entry.streams.at(location) = vertices.size();
            const std::span<const std::byte> bytes = std::as_bytes(std::span(stream));
            vertices.insert(vertices.end(), bytes.begin(), bytes.end());
        };
        append(0, mesh.positions);
        append(1, mesh.normals);
        append(2, mesh.tangents);
        append(3, mesh.uv0);
        append(4, mesh.uv1);
        append(5, mesh.colours);
        if (!vertices.empty())
        {
            entry.vertices = m_device.CreateBuffer({vertices.size(), BufferUsage::Vertex}, vertices, "Mesh vertices");
        }
        if (!mesh.indices.empty())
        {
            const std::span<const std::byte> indices = std::as_bytes(std::span(mesh.indices));
            entry.indices = m_device.CreateBuffer({indices.size(), BufferUsage::Index}, indices, "Mesh indices");
            entry.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
        }
        ++m_stats.uploads;
    }

    void SceneRenderer::DestroyMesh(MeshEntry& mesh)
    {
        m_device.Destroy(mesh.vertices);
        m_device.Destroy(mesh.indices);
    }

    void SceneRenderer::ReleaseMesh(AssetKey key)
    {
        const auto found = m_meshes.find(key);
        if (found != m_meshes.end())
        {
            DestroyMesh(found->second);
            m_meshes.erase(found);
        }
    }

    bool SceneRenderer::IsMaterialCurrent(AssetKey key, std::uint32_t revision) const
    {
        const auto found = m_materials.find(key);
        if (found == m_materials.end() || found->second.revision != revision)
        {
            return false;
        }
        const MaterialEntry& material = found->second;
        const auto shader = m_shaders.find(material.shaderKey);
        const ShaderEntry* now = shader == m_shaders.end() ? nullptr : &shader->second;
        if (now != material.shader || (now != nullptr && now->revision != material.shaderRevision))
        {
            return false;
        }
        // A render target it samples was made, remade at a new size or released since.
        return std::all_of(material.images.begin(), material.images.end(), [this](const SampledImage& image) {
            const auto target = m_targets.find(image.image);
            return (target == m_targets.end() ? TextureHandle{} : target->second.texture) == image.bound;
        });
    }

    void SceneRenderer::PrepareMaterial(AssetKey key, std::uint32_t revision, const MaterialData& material)
    {
        ReleaseMaterial(key);
        MaterialEntry& entry = m_materials[key];
        entry.revision = revision;
        entry.id = NextId();
        entry.doubleSided = material.doubleSided;
        entry.shaderKey = KeyOf(material.shader);
        const auto shader = m_shaders.find(entry.shaderKey);
        entry.shader = shader == m_shaders.end() ? nullptr : &shader->second;
        entry.shaderRevision = entry.shader == nullptr ? 0 : entry.shader->revision;
        if (entry.shader == nullptr || entry.shader->variant == nullptr)
        {
            // Drawn as the fallback material, which has no material group.
            return;
        }
        const ShaderVariant& variant = *shader->second.variant;

        std::vector<BindingGroupEntry> bindings;
        for (const ShaderBinding& binding : variant.bindings)
        {
            if (binding.set != MaterialSet)
            {
                continue;
            }
            BindingGroupEntry bound;
            bound.binding = binding.binding;
            if (binding.kind == ShaderBindingKind::Texture)
            {
                const TextureHandle fallback =
                    m_defaultTextures.at(static_cast<std::size_t>(DefaultTextureOf(variant, binding.name)));
                bound.texture = fallback;
                bound.sampler = m_defaultSampler;
                // A render target image binds its texture (ADR-0026). Other images are uploaded from renderer ticket
                // 13; until then they bind the default.
                const auto value = material.overrides.find(binding.name);
                const MaterialTexture* texture =
                    value == material.overrides.end() ? nullptr : std::get_if<MaterialTexture>(&value->second);
                if (texture != nullptr && !texture->image.IsEmpty())
                {
                    SampledImage image{bindings.size(), KeyOf(texture->image), {}, fallback};
                    if (const auto target = m_targets.find(image.image); target != m_targets.end())
                    {
                        image.bound = target->second.texture;
                        bound.texture = image.bound;
                        bound.sampler = SamplerFor(texture->sampler);
                    }
                    entry.images.push_back(image);
                }
            }
            else if (variant.materialBlockBinding == binding.binding && !variant.defaultBlock.empty())
            {
                // The default block image, then the material's overrides over it (ADR-0024).
                std::vector<std::byte> block = variant.defaultBlock;
                WriteOverrides(variant, material, block);
                entry.block = m_device.CreateBuffer({block.size(), BufferUsage::Uniform}, block, "Material block");
                bound.buffer = entry.block;
                ++m_stats.uploads;
            }
            bindings.push_back(bound);
        }
        entry.layout = shader->second.materialLayout;
        entry.bindings = bindings;
        if (!entry.layout.IsEmpty())
        {
            entry.group = m_device.CreateBindingGroup({entry.layout, std::move(bindings)}, "Material");
        }
    }

    BindingGroupHandle SceneRenderer::GroupWithout(MaterialEntry& material, const std::vector<AssetKey>& replaced)
    {
        std::vector<AssetKey> mine;
        for (const SampledImage& image : material.images)
        {
            if (!image.bound.IsEmpty() && std::find(replaced.begin(), replaced.end(), image.image) != replaced.end())
            {
                mine.push_back(image.image);
            }
        }
        if (mine.empty())
        {
            return material.group;
        }
        std::sort(mine.begin(), mine.end());
        mine.erase(std::unique(mine.begin(), mine.end()), mine.end());
        if (const auto found = material.withoutTargets.find(mine); found != material.withoutTargets.end())
        {
            return found->second;
        }
        std::vector<BindingGroupEntry> bindings = material.bindings;
        for (const SampledImage& image : material.images)
        {
            if (std::binary_search(mine.begin(), mine.end(), image.image))
            {
                bindings.at(image.binding).texture = image.fallback;
                bindings.at(image.binding).sampler = m_defaultSampler;
            }
        }
        const BindingGroupHandle group =
            m_device.CreateBindingGroup({material.layout, std::move(bindings)}, "Material");
        material.withoutTargets.emplace(std::move(mine), group);
        return group;
    }

    void SceneRenderer::DestroyMaterial(MaterialEntry& material)
    {
        for (const auto& [replaced, group] : material.withoutTargets)
        {
            m_device.Destroy(group);
        }
        m_device.Destroy(material.group);
        m_device.Destroy(material.block);
    }

    void SceneRenderer::ReleaseMaterial(AssetKey key)
    {
        const auto found = m_materials.find(key);
        if (found != m_materials.end())
        {
            DestroyMaterial(found->second);
            m_materials.erase(found);
        }
    }

    std::vector<AssetKey> SceneRenderer::ShaderKeys() const
    {
        std::vector<AssetKey> keys;
        for (const auto& [key, entry] : m_shaders)
        {
            keys.push_back(key);
        }
        return keys;
    }

    std::vector<AssetKey> SceneRenderer::MeshKeys() const
    {
        std::vector<AssetKey> keys;
        for (const auto& [key, entry] : m_meshes)
        {
            keys.push_back(key);
        }
        return keys;
    }

    std::vector<AssetKey> SceneRenderer::MaterialKeys() const
    {
        std::vector<AssetKey> keys;
        for (const auto& [key, entry] : m_materials)
        {
            keys.push_back(key);
        }
        return keys;
    }

    // ---- The frame ----

    void SceneRenderer::SetFrame(SceneFrame frame)
    {
        m_frame = std::move(frame);
    }

    SceneStats SceneRenderer::TakeStats()
    {
        const SceneStats stats = m_stats;
        m_stats.uploads = 0;
        return stats;
    }

    const SceneRenderer::PipelineEntry& SceneRenderer::MeshPipeline(const ShaderEntry& shader, const MeshEntry& mesh,
                                                                    bool doubleSided)
    {
        const std::uint32_t read = shader.variant != nullptr ? ReadMask(*shader.variant) : 0;
        std::uint32_t present = 0;
        for (std::uint32_t location = 0; location < Streams.size(); ++location)
        {
            if (mesh.streams.at(location).has_value())
            {
                present |= 1u << location;
            }
        }
        const PipelineKey key{&shader, read & present, doubleSided, m_samples};
        if (const auto found = m_pipelines.find(key); found != m_pipelines.end())
        {
            return found->second;
        }

        PipelineEntry& entry = m_pipelines[key];
        entry.id = NextId();
        if (shader.variant == nullptr)
        {
            entry.failure = "it has no variant to draw";
            return entry;
        }
        entry.failure = CheckFixedSets(*shader.variant);
        PipelineDesc desc;
        desc.shader = shader.variant;
        desc.sets = {m_frameLayout, m_viewLayout, shader.materialLayout, m_drawLayout};
        for (const ShaderVertexInput& input : shader.variant->vertexInputs)
        {
            if (input.location >= Streams.size())
            {
                entry.failure = std::format("it reads vertex input '{}' at location {}, which no mesh stream feeds",
                                            input.name, input.location);
                break;
            }
            const StreamFormat& stream = Streams.at(input.location);
            // A stream the mesh lacks reads one constant: a stride of 0 (ADR-0024).
            const bool has = (key.streams & (1u << input.location)) != 0;
            desc.vertexBuffers.push_back(
                {has ? static_cast<std::uint32_t>(stream.size) : 0u, {{input.location, stream.format, 0}}});
            entry.locations.push_back(input.location);
        }
        desc.cull = doubleSided ? CullMode::None : CullMode::Back;
        desc.colourFormats = {TextureFormat::Rgba16Float};
        desc.samples = m_samples;
        if (entry.failure.empty())
        {
            PipelineResult made = m_device.CreatePipeline(desc, shader.container.name);
            entry.pipeline = made.pipeline;
            entry.failure = std::move(made.diagnostic);
        }
        if (!entry.failure.empty())
        {
            m_logger.Error("FenrirRenderer: no pipeline for shader '{}', so its materials draw as the fallback "
                           "material: {}",
                           shader.container.name, entry.failure);
        }
        return entry;
    }

    std::vector<SceneRenderer::ResolvedDraw> SceneRenderer::ResolveDraws()
    {
        std::vector<ResolvedDraw> draws;
        draws.reserve(m_frame.draws.size());
        for (const DrawRequest& request : m_frame.draws)
        {
            const auto mesh = m_meshes.find(request.mesh);
            if (mesh == m_meshes.end() || mesh->second.indexCount == 0)
            {
                continue;
            }
            const auto material = m_materials.find(request.material);
            MaterialEntry* found = material == m_materials.end() ? nullptr : &material->second;
            const ShaderEntry* shader =
                found != nullptr && found->shader != nullptr && found->shader->variant != nullptr ? found->shader
                                                                                                  : &m_fallback;
            const PipelineEntry* pipeline =
                &MeshPipeline(*shader, mesh->second, found != nullptr && found->doubleSided);
            if (pipeline->pipeline.IsEmpty() && shader != &m_fallback)
            {
                shader = &m_fallback;
                pipeline = &MeshPipeline(m_fallback, mesh->second, false);
            }
            if (pipeline->pipeline.IsEmpty())
            {
                continue;
            }

            if (shader->variant != nullptr)
            {
                for (const ShaderVertexInput& input : shader->variant->vertexInputs)
                {
                    if (input.location < Streams.size() && !mesh->second.streams.at(input.location).has_value() &&
                        m_loggedStreams.insert({request.mesh, input.location}).second)
                    {
                        const StreamFormat& stream = Streams.at(input.location);
                        m_logger.Warn("FenrirRenderer: mesh {} has no {}, which shader '{}' reads; it is drawn with "
                                      "{}",
                                      request.mesh.index, stream.name, shader->container.name, stream.defaultName);
                    }
                }
            }

            const bool fallback = shader == &m_fallback;
            ResolvedDraw draw;
            draw.pipeline = pipeline;
            draw.material = fallback ? nullptr : found;
            draw.mesh = &mesh->second;
            draw.world = request.world;
            const std::uint64_t materialId = fallback ? 0 : found->id;
            draw.sortKey = (std::uint64_t{OpaqueStage} << 56u) | ((std::uint64_t{pipeline->id} & 0xFFFFu) << 40u) |
                           ((materialId & 0xFFFFFu) << 20u) | (std::uint64_t{mesh->second.id} & 0xFFFFFu);
            draws.push_back(draw);
        }
        std::stable_sort(draws.begin(), draws.end(),
                         [](const ResolvedDraw& a, const ResolvedDraw& b) { return a.sortKey < b.sortKey; });
        return draws;
    }

    BindingGroupHandle SceneRenderer::OutputGroup(TextureHandle sceneColour)
    {
        const std::pair<std::uint32_t, std::uint32_t> key{sceneColour.index, sceneColour.generation};
        if (const auto found = m_outputGroups.find(key); found != m_outputGroups.end())
        {
            return found->second;
        }
        BindingGroupEntry entry;
        entry.binding = 0;
        entry.texture = sceneColour;
        entry.sampler = m_outputSampler;
        const BindingGroupHandle group = m_device.CreateBindingGroup({m_outputLayout, {entry}}, "Output");
        m_outputGroups.emplace(key, group);
        return group;
    }

    void SceneRenderer::AddWindowClear(RenderGraph& graph, const FrameContext& frame)
    {
        // A window no view draws into is cleared to black.
        const GraphResource backbuffer = frame.backbuffer;
        graph.AddPass(
            "Clear", RenderStage::Output, QueueKind::Graphics,
            [=](PassBuilder& pass) { pass.ColourAttachment(backbuffer, LoadOp::Clear, {0.0f, 0.0f, 0.0f, 1.0f}); },
            [](PassContext&) {});
    }

    void SceneRenderer::AddPasses(RenderGraph& graph, const FrameContext& frame)
    {
        // The Output groups of SceneColor textures the transient pool has let go of.
        for (auto it = m_outputGroups.begin(); it != m_outputGroups.end();)
        {
            if (!m_device.IsLive(TextureHandle{it->first.first, it->first.second}))
            {
                m_device.Destroy(it->second);
                it = m_outputGroups.erase(it);
            }
            else
            {
                ++it;
            }
        }

        m_stats.draws = 0;
        if (m_frame.views.empty())
        {
            AddWindowClear(graph, frame);
            return;
        }

        const auto draws = std::make_shared<const std::vector<ResolvedDraw>>(ResolveDraws());
        const std::uint64_t frameIndex = frame.frameIndex;
        const SceneLighting& lighting = m_frame.lighting;
        const FrameBlock frameBlock{Math::Vec4(lighting.lightDirection, 0.0f), Math::Vec4(lighting.lightColour, 0.0f),
                                    Math::Vec4(lighting.ambient, 0.0f),
                                    Math::Vec4(m_frame.time, m_frame.deltaTime, 0.0f, 0.0f)};
        m_frameBlocks->Write(frameIndex, 0, BytesOf(frameBlock));
        m_drawBlocks->Reserve(draws->size());
        for (std::size_t i = 0; i < draws->size(); ++i)
        {
            const Math::Mat4& world = (*draws)[i].world;
            const DrawBlock block{world, glm::transpose(glm::inverse(world))};
            m_drawBlocks->Write(frameIndex, i, BytesOf(block));
        }
        const std::vector<ViewRequest>& views = m_frame.views;
        m_viewBlocks->Reserve(views.size());
        m_backgroundBlocks->Reserve(views.size());

        // The render target images this frame's draws sample, each imported into the graph once.
        std::set<AssetKey> sampled;
        for (const ResolvedDraw& draw : *draws)
        {
            if (draw.material != nullptr)
            {
                for (const SampledImage& image : draw.material->images)
                {
                    if (!image.bound.IsEmpty() && m_targets.contains(image.image))
                    {
                        sampled.insert(image.image);
                    }
                }
            }
        }
        std::map<AssetKey, GraphResource> images;
        const auto imported = [&](AssetKey key) {
            if (const auto found = images.find(key); found != images.end())
            {
                return found->second;
            }
            const TargetEntry& target = m_targets.at(key);
            TextureDesc desc;
            desc.format = TextureFormat::Rgba8Srgb;
            desc.width = target.size.width;
            desc.height = target.size.height;
            desc.usage = TextureUsage::ColourAttachment | TextureUsage::Sampled;
            const GraphResource resource =
                graph.ImportTexture(std::format("Image {}", key.index), target.texture, desc);
            images.emplace(key, resource);
            return resource;
        };

        // Which sampled images each view may read. A view samples an image after every view that draws it, so the
        // graph orders them; one that would wait on itself, directly or through other views, gets the default texture
        // for that image instead (ADR-0026), the views taken in their order.
        std::vector<std::vector<std::size_t>> waitsOn(views.size());
        std::vector<std::vector<AssetKey>> replaced(views.size());
        std::vector<std::vector<AssetKey>> reads(views.size());
        for (std::size_t v = 0; v < views.size(); ++v)
        {
            for (const AssetKey image : sampled)
            {
                std::vector<std::size_t> writers;
                for (std::size_t w = 0; w < views.size(); ++w)
                {
                    if (views[w].target == image && m_targets.contains(image))
                    {
                        writers.push_back(w);
                    }
                }
                const bool own = views[v].target == image;
                const bool cycle =
                    std::any_of(writers.begin(), writers.end(), [&](std::size_t w) { return Waits(waitsOn, w, v); });
                if (own || cycle)
                {
                    replaced[v].push_back(image);
                    if (m_loggedSampling.insert({views[v].target, image}).second)
                    {
                        m_logger.Warn(own ? "FenrirRenderer: a view into {} draws materials that sample its own target "
                                            "image, so they sample the default texture there"
                                          : "FenrirRenderer: a view into {} draws materials that sample image {}, "
                                            "whose views wait on it in a cycle, so they sample the default texture "
                                            "there",
                                      TargetName(views[v].target), image.index);
                    }
                    continue;
                }
                waitsOn[v].insert(waitsOn[v].end(), writers.begin(), writers.end());
                reads[v].push_back(image);
            }
        }

        const BindingGroupHandle frameGroup = m_frameBlocks->Group(frameIndex, 0);
        const BindingGroupHandle drawGroup = m_drawBlocks->Group(frameIndex, 0);
        // The targets drawn into so far: the first view into each clears it. The window's key is empty.
        std::set<AssetKey> cleared;
        for (std::size_t v = 0; v < views.size(); ++v)
        {
            const ViewRequest& request = views[v];
            const bool intoImage = !request.target.IsEmpty();
            if (intoImage && !m_targets.contains(request.target))
            {
                continue;
            }
            const GraphResource target = intoImage ? imported(request.target) : frame.backbuffer;
            const PixelRect rect = request.viewport;
            const PixelRect viewport{0, 0, rect.width, rect.height};

            const Math::Mat4 viewProjection = request.projection * request.view;
            const ViewBlock viewBlock{request.view,
                                      request.projection,
                                      viewProjection,
                                      glm::inverse(viewProjection),
                                      Math::Vec4(request.position, 1.0f),
                                      Math::Vec4(static_cast<float>(rect.x), static_cast<float>(rect.y),
                                                 static_cast<float>(rect.width), static_cast<float>(rect.height))};
            m_viewBlocks->Write(frameIndex, v, BytesOf(viewBlock));
            const BackgroundBlock backgroundBlock{
                Math::Vec4(request.background.r, request.background.g, request.background.b, request.background.a)};
            m_backgroundBlocks->Write(frameIndex, v, BytesOf(backgroundBlock));
            const BindingGroupHandle viewGroup = m_viewBlocks->Group(frameIndex, v);
            const BindingGroupHandle backgroundGroup = m_backgroundBlocks->Group(frameIndex, v);

            // Each draw's material group in this view, with the images it may not sample replaced.
            auto groups = std::make_shared<std::vector<BindingGroupHandle>>();
            groups->reserve(draws->size());
            for (const ResolvedDraw& draw : *draws)
            {
                groups->push_back(draw.material == nullptr ? BindingGroupHandle{}
                                                           : GroupWithout(*draw.material, replaced[v]));
            }
            std::vector<GraphResource> samples;
            for (const AssetKey image : reads[v])
            {
                samples.push_back(imported(image));
            }

            TextureDesc colourDesc;
            colourDesc.format = TextureFormat::Rgba16Float;
            colourDesc.width = rect.width;
            colourDesc.height = rect.height;
            colourDesc.samples = m_samples;
            colourDesc.usage = TextureUsage::ColourAttachment | TextureUsage::Sampled;
            TextureDesc depthDesc = colourDesc;
            depthDesc.format = TextureFormat::Depth32Float;
            depthDesc.usage = TextureUsage::DepthAttachment;
            const GraphResource sceneColour = graph.CreateTexture("SceneColor", colourDesc);
            const GraphResource sceneDepth = graph.CreateTexture("SceneDepth", depthDesc);

            graph.AddPass(
                "Opaque", RenderStage::Opaque, QueueKind::Graphics,
                [=](PassBuilder& pass) {
                    pass.ColourAttachment(sceneColour, LoadOp::Clear, {0.0f, 0.0f, 0.0f, 0.0f});
                    pass.DepthAttachment(sceneDepth, LoadOp::Clear, 0.0f);
                    for (const GraphResource image : samples)
                    {
                        pass.Sample(image);
                    }
                },
                [this, draws, groups, frameGroup, viewGroup, drawGroup, viewport](PassContext& context) {
                    PassEncoder& encoder = context.Encoder();
                    encoder.SetViewport(viewport);
                    const PipelineEntry* current = nullptr;
                    BindingGroupHandle material;
                    for (std::size_t i = 0; i < draws->size(); ++i)
                    {
                        const ResolvedDraw& draw = (*draws)[i];
                        const BindingGroupHandle group = (*groups)[i];
                        const bool newPipeline = draw.pipeline != current;
                        if (newPipeline)
                        {
                            current = draw.pipeline;
                            encoder.SetPipeline(current->pipeline);
                            encoder.SetBindingGroup(0, frameGroup);
                            encoder.SetBindingGroup(1, viewGroup);
                            encoder.SetBindingGroup(3, drawGroup);
                        }
                        if (!group.IsEmpty() && (newPipeline || group != material))
                        {
                            encoder.SetBindingGroup(MaterialSet, group);
                        }
                        material = group;
                        for (std::size_t slot = 0; slot < current->locations.size(); ++slot)
                        {
                            const std::uint32_t location = current->locations[slot];
                            const std::optional<std::size_t>& offset = draw.mesh->streams.at(location);
                            if (offset.has_value())
                            {
                                encoder.SetVertexBuffer(static_cast<std::uint32_t>(slot), draw.mesh->vertices, *offset);
                            }
                            else
                            {
                                encoder.SetVertexBuffer(static_cast<std::uint32_t>(slot), m_defaultStreams,
                                                        Streams.at(location).defaultOffset);
                            }
                        }
                        encoder.SetIndexBuffer(draw.mesh->indices, IndexFormat::UInt32);
                        encoder.DrawIndexed(
                            {.indexCount = draw.mesh->indexCount, .firstInstance = static_cast<std::uint32_t>(i)});
                    }
                });
            m_stats.draws += static_cast<std::uint32_t>(draws->size());

            graph.AddPass(
                "Background", RenderStage::Background, QueueKind::Graphics,
                [=](PassBuilder& pass) {
                    pass.ColourAttachment(sceneColour, LoadOp::Load);
                    pass.DepthReadOnly(sceneDepth);
                },
                [this, backgroundGroup, viewport](PassContext& context) {
                    PassEncoder& encoder = context.Encoder();
                    encoder.SetViewport(viewport);
                    encoder.SetPipeline(m_backgroundPipeline);
                    encoder.SetBindingGroup(MaterialSet, backgroundGroup);
                    encoder.Draw({.vertexCount = 3});
                });

            // The first view into a target clears all of it; each later one draws over it, in its viewport only.
            const LoadOp load = cleared.insert(request.target).second ? LoadOp::Clear : LoadOp::Load;
            graph.AddPass(
                "Output", RenderStage::Output, QueueKind::Graphics,
                [=](PassBuilder& pass) {
                    pass.Sample(sceneColour);
                    pass.ColourAttachment(target, load, {0.0f, 0.0f, 0.0f, 1.0f});
                },
                [this, sceneColour, rect](PassContext& context) {
                    PassEncoder& encoder = context.Encoder();
                    encoder.SetViewport(rect);
                    encoder.SetPipeline(m_outputPipeline);
                    encoder.SetBindingGroup(MaterialSet, OutputGroup(context.Texture(sceneColour)));
                    encoder.Draw({.vertexCount = 3});
                });
        }
        if (!cleared.contains(AssetKey{}))
        {
            AddWindowClear(graph, frame);
        }
    }

    Math::Mat4 ReversedZOrthographic(float height, float aspect, float near, float far)
    {
        Math::Mat4 projection(1.0f);
        projection[0][0] = 2.0f / (height * aspect);
        projection[1][1] = 2.0f / height;
        // Depth is (far + z) / (far - near) for view-space z = -distance: 1 at the near plane, 0 at the far one.
        projection[2][2] = 1.0f / (far - near);
        projection[3][2] = far / (far - near);
        return projection;
    }

    PixelRect ViewportPixels(float x, float y, float width, float height, PixelSize size)
    {
        const auto edge = [](float fraction, std::uint32_t pixels) {
            const float at = std::round(fraction * static_cast<float>(pixels));
            return static_cast<std::int64_t>(std::clamp(at, 0.0f, static_cast<float>(pixels)));
        };
        const std::int64_t left = edge(x, size.width);
        const std::int64_t right = edge(x + width, size.width);
        const std::int64_t top = edge(y, size.height);
        const std::int64_t bottom = edge(y + height, size.height);
        if (right <= left || bottom <= top)
        {
            return {};
        }
        return {static_cast<std::int32_t>(left), static_cast<std::int32_t>(top),
                static_cast<std::uint32_t>(right - left), static_cast<std::uint32_t>(bottom - top)};
    }

    Math::Mat4 ReversedZPerspective(float verticalFov, float aspect, float near)
    {
        const float focal = 1.0f / std::tan(verticalFov * 0.5f);
        Math::Mat4 projection(0.0f);
        projection[0][0] = focal / aspect;
        projection[1][1] = focal;
        // Depth is near / -z: 1 at the near plane, falling to 0 at infinity.
        projection[3][2] = near;
        projection[2][3] = -1.0f;
        return projection;
    }
} // namespace Fenrir::Render
