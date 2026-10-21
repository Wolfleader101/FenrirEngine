#include "RendererCore.hpp"

#include "DebugLine.hpp"
#include "Lit.hpp"
#include "Output.hpp"
#include "Sky.hpp"
#include "Unlit.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Fenrir::Render
{
    namespace
    {
        constexpr std::size_t kViewSlotStride = 512; // ViewData is 288 bytes; UBO offsets want 256-byte alignment
        constexpr std::size_t kMaxViews = 8;
        constexpr std::size_t kDrawItemSize = sizeof(float) * 32; // world and normal matrices

        template <typename T>
        std::span<const std::byte> Bytes(const T& value)
        {
            return std::as_bytes(std::span<const T>(&value, 1));
        }

        template <typename T>
        std::span<const std::byte> Bytes(const std::vector<T>& values)
        {
            return std::as_bytes(std::span<const T>(values));
        }

        struct FrameData
        {
            Math::Vec4 lightDirection;
            Math::Vec4 lightColour;
            Math::Vec4 ambient;
            Math::Vec4 time;
        };

        struct ViewData
        {
            Math::Mat4 view;
            Math::Mat4 projection;
            Math::Mat4 viewProjection;
            Math::Mat4 inverseViewProjection;
            Math::Vec4 cameraPosition;
            Math::Vec4 viewport;
        };

        Gpu::VertexLayout MeshVertexLayout()
        {
            Gpu::VertexLayout layout;
            layout.stride = sizeof(Vertex);
            layout.attributes = {{0, Gpu::VertexFormat::Float3, offsetof(Vertex, position)},
                                 {1, Gpu::VertexFormat::Float3, offsetof(Vertex, normal)},
                                 {2, Gpu::VertexFormat::Float2, offsetof(Vertex, uv)},
                                 {3, Gpu::VertexFormat::Float4, offsetof(Vertex, colour)}};
            return layout;
        }
    } // namespace

    Math::Mat4 ReversedInfinitePerspective(float fovY, float aspect, float near)
    {
        const float f = 1.0f / std::tan(fovY * 0.5f);
        Math::Mat4 p(0.0f);
        p[0][0] = f / aspect;
        p[1][1] = f;
        p[2][3] = -1.0f; // w = -z
        p[3][2] = near;  // depth = near / -z: 1 at the near plane, 0 at infinity
        return p;
    }

    RendererCore::RendererCore(std::unique_ptr<Gpu::GraphicsDevice> device, std::function<void(const std::string&)> log)
        : m_device(std::move(device)), m_log(std::move(log))
    {
    }

    RendererCore::~RendererCore() = default;

    void RendererCore::LogOnce(const std::string& message)
    {
        if (m_logged.insert(message).second)
        {
            m_log(message);
        }
    }

    bool RendererCore::LoadProgram(const char* name, std::span<const std::uint8_t> bytes, std::string& error)
    {
        std::optional<ShaderContainer> container = ReadShaderContainer(bytes);
        if (!container)
        {
            error = std::string("shader container ") + name + " is malformed";
            return false;
        }
        Program program;
        program.name = name;
        program.container = std::move(*container);
        program.vertex = m_device->CreateShader({Gpu::ShaderStage::Vertex, program.container.vertex}, error);
        if (!program.vertex.IsValid())
        {
            error = std::string(name) + ".vert: " + error;
            return false;
        }
        program.fragment = m_device->CreateShader({Gpu::ShaderStage::Fragment, program.container.fragment}, error);
        if (!program.fragment.IsValid())
        {
            error = std::string(name) + ".frag: " + error;
            return false;
        }

        // Sets 0, 1 and 3 are declared in C++; debug builds check them against the reflection (ADR-0018).
        const std::array<std::pair<std::uint32_t, Gpu::BindingKind>, 3> declared = {
            {{0, Gpu::BindingKind::UniformBuffer},
             {1, Gpu::BindingKind::UniformBuffer},
             {3, Gpu::BindingKind::StorageBuffer}}};
        program.sets = {m_emptyLayout, m_emptyLayout, m_emptyLayout, m_emptyLayout};
        for (const auto& [set, kind] : declared)
        {
            for (const ReflectedBinding* b : program.container.Set(set))
            {
                if (b->binding != 0 || b->kind != kind)
                {
                    error = std::string(name) + ": set " + std::to_string(set) + " binding " +
                            std::to_string(b->binding) + " does not match the declared layout";
                    return false;
                }
                program.sets[set] = set == 0 ? m_frameLayout : set == 1 ? m_viewLayout : m_drawLayout;
            }
        }
        // Set 2 is reflected (ADR-0024).
        Gpu::BindGroupLayoutDesc material;
        for (const ReflectedBinding* b : program.container.Set(2))
        {
            material.entries.push_back({b->binding, b->kind});
        }
        if (!material.entries.empty())
        {
            program.sets[2] = m_device->CreateBindGroupLayout(material);
        }
        m_programs.push_back(std::move(program));
        for (const ReflectedBinding& b : m_programs.back().container.bindings)
        {
            if (b.set == 2 && b.binding == 0 && b.kind == Gpu::BindingKind::UniformBuffer)
            {
                m_programs.back().params = &b;
            }
        }
        return true;
    }

    std::size_t RendererCore::ProgramIndex(const std::string& name) const
    {
        for (std::size_t i = 0; i < m_programs.size(); ++i)
        {
            if (m_programs[i].name == name)
                return i;
        }
        return SIZE_MAX;
    }

    bool RendererCore::Init(std::string& error)
    {
        m_frameLayout = m_device->CreateBindGroupLayout({{{0, Gpu::BindingKind::UniformBuffer}}});
        m_viewLayout = m_device->CreateBindGroupLayout({{{0, Gpu::BindingKind::UniformBuffer}}});
        m_drawLayout = m_device->CreateBindGroupLayout({{{0, Gpu::BindingKind::StorageBuffer}}});
        m_emptyLayout = m_device->CreateBindGroupLayout({});

        m_programs.reserve(8);
        if (!LoadProgram("builtin:lit", Shaders::Lit, error) || !LoadProgram("builtin:unlit", Shaders::Unlit, error) ||
            !LoadProgram("sky", Shaders::Sky, error) || !LoadProgram("output", Shaders::Output, error) ||
            !LoadProgram("debugLine", Shaders::DebugLine, error))
        {
            return false;
        }

        // Defaults instead of variant keys (ADR-0024).
        Gpu::TextureDesc white{Gpu::TextureFormat::Rgba8Srgb, 1, 1, 1, false, false, "default white"};
        m_white = m_device->CreateTexture(white);
        const std::uint32_t whitePixel = 0xFFFFFFFF;
        m_device->WriteTexture(m_white, 0, Bytes(whitePixel));
        Gpu::TextureDesc cube{Gpu::TextureFormat::Rgba8Srgb, 1, 1, 1, true, false, "default cube"};
        m_blackCube = m_device->CreateTexture(cube);
        const std::uint32_t blackPixel = 0xFF000000;
        for (int face = 0; face < 6; ++face)
            m_device->WriteTexture(m_blackCube, face, Bytes(blackPixel));
        m_repeatSampler = m_device->CreateSampler({true, true, true, 8.0f});
        m_clampSampler = m_device->CreateSampler({true, false, false, 1.0f});

        const Program& sky = m_programs[ProgramIndex("sky")];
        Gpu::PipelineDesc skyDesc;
        skyDesc.vertex = sky.vertex;
        skyDesc.fragment = sky.fragment;
        skyDesc.sets = sky.sets;
        skyDesc.cullBack = false;
        skyDesc.depthWrite = false; // depth 0 against a cleared 0: only uncovered pixels pass GreaterEqual
        skyDesc.label = "sky";
        m_skyPipeline = m_device->CreatePipeline(skyDesc);

        const Program& line = m_programs[ProgramIndex("debugLine")];
        Gpu::PipelineDesc lineDesc;
        lineDesc.vertex = line.vertex;
        lineDesc.fragment = line.fragment;
        lineDesc.sets = line.sets;
        lineDesc.vertexLayout.stride = sizeof(DebugLineVertex);
        lineDesc.vertexLayout.attributes = {{0, Gpu::VertexFormat::Float3, offsetof(DebugLineVertex, position)},
                                            {3, Gpu::VertexFormat::Float4, offsetof(DebugLineVertex, colour)}};
        lineDesc.topology = Gpu::Topology::Lines;
        lineDesc.cullBack = false;
        lineDesc.depthWrite = false;
        lineDesc.label = "debug lines";
        m_linePipeline = m_device->CreatePipeline(lineDesc);

        const Program& output = m_programs[ProgramIndex("output")];
        Gpu::PipelineDesc outDesc;
        outDesc.vertex = output.vertex;
        outDesc.fragment = output.fragment;
        outDesc.sets = output.sets;
        outDesc.cullBack = false;
        outDesc.depthTest = false;
        outDesc.depthWrite = false;
        outDesc.colourFormat = Gpu::TextureFormat::Rgba8Srgb;
        outDesc.depthFormat = Gpu::TextureFormat::None;
        outDesc.label = "output";
        m_outputPipeline = m_device->CreatePipeline(outDesc);
        if (!m_skyPipeline.IsValid() || !m_linePipeline.IsValid() || !m_outputPipeline.IsValid())
        {
            error = "a built-in pipeline failed to link";
            return false;
        }

        // The fallback: flat magenta unlit (ADR-0023).
        MaterialData fallback;
        fallback.shader = "builtin:unlit";
        fallback.params["baseColour"] = Colour{1, 0, 1, 1};
        PrepareMaterial(0, 1, fallback);
        m_fallback = m_materials[0];
        m_materials.erase(0);

        for (FrameSlot& slot : m_slots)
        {
            slot.frameUbo = m_device->CreateBuffer(
                {sizeof(FrameData), static_cast<std::uint32_t>(Gpu::BufferUsage::Uniform), "frame"});
            slot.frameGroup = m_device->CreateBindGroup({m_frameLayout, {{0, slot.frameUbo, 0, sizeof(FrameData)}}});
            slot.viewUbo = m_device->CreateBuffer(
                {kViewSlotStride * kMaxViews, static_cast<std::uint32_t>(Gpu::BufferUsage::Uniform), "views"});
            for (std::size_t v = 0; v < kMaxViews; ++v)
            {
                slot.viewGroups.push_back(m_device->CreateBindGroup(
                    {m_viewLayout, {{0, slot.viewUbo, v * kViewSlotStride, sizeof(ViewData)}}}));
            }
        }
        return true;
    }

    bool RendererCore::IsMeshCurrent(AssetKey key, std::uint32_t revision) const
    {
        auto it = m_meshes.find(key);
        return it != m_meshes.end() && it->second.revision == revision;
    }

    void RendererCore::PrepareMesh(AssetKey key, std::uint32_t revision, const MeshData& mesh)
    {
        GpuMesh& gpu = m_meshes[key];
        if (gpu.vertices.IsValid())
        {
            m_device->Destroy(gpu.vertices);
            m_device->Destroy(gpu.indices);
        }
        gpu.revision = revision;
        gpu.vertices = m_device->CreateBuffer({mesh.vertices.size() * sizeof(Vertex),
                                               static_cast<std::uint32_t>(Gpu::BufferUsage::Vertex), "mesh vertices"},
                                              Bytes(mesh.vertices));
        gpu.indices = m_device->CreateBuffer({mesh.indices.size() * sizeof(std::uint32_t),
                                              static_cast<std::uint32_t>(Gpu::BufferUsage::Index), "mesh indices"},
                                             Bytes(mesh.indices));
        gpu.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
        ++m_uploads;
    }

    bool RendererCore::IsImageCurrent(AssetKey key, std::uint32_t revision) const
    {
        auto it = m_images.find(key);
        return it != m_images.end() && it->second.revision == revision;
    }

    void RendererCore::PrepareImage(AssetKey key, std::uint32_t revision, const ImageData& image)
    {
        GpuImage& gpu = m_images[key];
        if (gpu.texture.IsValid())
        {
            m_device->Destroy(gpu.texture);
        }
        Gpu::TextureDesc desc;
        desc.format =
            image.format == ImageFormat::Rgba8Srgb ? Gpu::TextureFormat::Rgba8Srgb : Gpu::TextureFormat::Rgba8Unorm;
        desc.width = image.width;
        desc.height = image.height;
        desc.cube = image.cube;
        desc.mipLevels =
            1 + static_cast<int>(std::floor(std::log2(static_cast<float>(std::max(image.width, image.height)))));
        desc.label = image.cube ? "cubemap" : "image";
        gpu.texture = m_device->CreateTexture(desc);
        gpu.cube = image.cube;
        gpu.revision = revision;
        const std::size_t faceBytes = static_cast<std::size_t>(image.width * image.height * 4);
        const int faces = image.cube ? 6 : 1;
        if (image.pixels.size() >= faceBytes * static_cast<std::size_t>(faces))
        {
            for (int face = 0; face < faces; ++face)
            {
                m_device->WriteTexture(
                    gpu.texture, face,
                    std::as_bytes(
                        std::span(image.pixels).subspan(faceBytes * static_cast<std::size_t>(face), faceBytes)));
            }
        }
        ++m_uploads;
    }

    bool RendererCore::IsMaterialCurrent(AssetKey key, std::uint32_t revision) const
    {
        auto it = m_materials.find(key);
        return it != m_materials.end() && it->second.revision == revision;
    }

    Gpu::BindGroupHandle RendererCore::MakeMaterialGroup(const Program& program, Gpu::BufferHandle params,
                                                         const MaterialData* material)
    {
        Gpu::BindGroupDesc desc;
        desc.layout = program.sets[2];
        for (const ReflectedBinding* b : program.container.Set(2))
        {
            if (b->kind == Gpu::BindingKind::UniformBuffer)
            {
                desc.entries.push_back({b->binding, params, 0, b->blockSize, {}, {}});
                continue;
            }
            Gpu::TextureHandle texture = m_white;
            if (material != nullptr)
            {
                auto it = material->params.find(b->name);
                if (it != material->params.end())
                {
                    if (const auto* image = std::get_if<AssetHandle<ImageData>>(&it->second))
                    {
                        auto found = m_images.find(Key(image->index, image->generation));
                        if (found != m_images.end())
                            texture = found->second.texture;
                    }
                }
            }
            desc.entries.push_back({b->binding, {}, 0, 0, texture, m_repeatSampler});
        }
        return m_device->CreateBindGroup(desc);
    }

    void RendererCore::PrepareMaterial(AssetKey key, std::uint32_t revision, const MaterialData& material)
    {
        GpuMaterial& gpu = m_materials[key];
        const std::size_t program = ProgramIndex(material.shader);
        if (program == SIZE_MAX || m_programs[program].params == nullptr)
        {
            LogOnce("material uses unknown shader '" + material.shader + "': drawn with the fallback");
            gpu = m_fallback;
            gpu.revision = revision;
            return;
        }
        if (gpu.params.IsValid())
        {
            m_device->Destroy(gpu.params);
            m_device->Destroy(gpu.group);
        }
        const Program& p = m_programs[program];
        const ReflectedBinding& block = *p.params;
        std::vector<std::byte> bytes(block.blockSize);

        // A std140 block cannot carry initialisers, so "the shader's GLSL default" (ADR-0024) does not exist: the
        // built-ins' defaults live here, everything else starts at zero.
        std::map<std::string, MaterialValue> values = {{"baseColour", Colour{1, 1, 1, 1}},
                                                       {"emissive", Colour{0, 0, 0, 0}},
                                                       {"metallic", 0.0f},
                                                       {"roughness", 0.5f}};
        for (const auto& [name, value] : material.params)
            values[name] = value;

        for (const auto& [name, value] : values)
        {
            const ReflectedMember* member = nullptr;
            for (const ReflectedMember& m : block.members)
                if (m.name == name)
                    member = &m;
            const bool isTexture = std::holds_alternative<AssetHandle<ImageData>>(value);
            if (isTexture)
                continue;
            if (member == nullptr)
            {
                if (material.params.count(name))
                    LogOnce("material parameter '" + name + "' is not in " + material.shader);
                continue;
            }
            float floats[4] = {0, 0, 0, 0};
            std::size_t count = 0;
            if (const float* f = std::get_if<float>(&value))
            {
                floats[0] = *f;
                count = 1;
            }
            else if (const Math::Vec4* v = std::get_if<Math::Vec4>(&value))
            {
                std::memcpy(floats, v, sizeof(floats));
                count = 4;
            }
            else if (const Colour* c = std::get_if<Colour>(&value))
            {
                floats[0] = c->r, floats[1] = c->g, floats[2] = c->b, floats[3] = c->a;
                count = 4;
            }
            if (count * 4 != member->size)
            {
                LogOnce("material parameter '" + name + "' has the wrong type for " + material.shader);
                continue;
            }
            std::memcpy(bytes.data() + member->offset, floats, member->size);
        }

        gpu.revision = revision;
        gpu.program = program;
        gpu.blend = material.blend;
        gpu.doubleSided = material.doubleSided;
        gpu.params = m_device->CreateBuffer(
            {bytes.size(), static_cast<std::uint32_t>(Gpu::BufferUsage::Uniform), "material params"},
            std::span<const std::byte>(bytes));
        gpu.group = MakeMaterialGroup(p, gpu.params, &material);
        ++m_uploads;
    }

    Gpu::PipelineHandle RendererCore::MeshPipeline(std::size_t program, BlendMode blend, bool doubleSided)
    {
        auto key = std::make_tuple(program, static_cast<int>(blend), doubleSided);
        if (auto it = m_pipelines.find(key); it != m_pipelines.end())
            return it->second;
        const Program& p = m_programs[program];
        Gpu::PipelineDesc desc;
        desc.vertex = p.vertex;
        desc.fragment = p.fragment;
        desc.sets = p.sets;
        desc.vertexLayout = MeshVertexLayout();
        desc.cullBack = !doubleSided;
        desc.depthWrite = blend == BlendMode::Opaque;
        desc.blend = blend == BlendMode::AlphaBlend ? Gpu::Blend::Alpha : Gpu::Blend::None;
        desc.label = p.name.c_str();
        Gpu::PipelineHandle handle = m_device->CreatePipeline(desc); // made lazily and cached (ADR-0024)
        m_pipelines[key] = handle;
        return handle;
    }

    void RendererCore::SkipFrame()
    {
        ++m_frame;
        m_device->BeginFrame(m_frame);
    }

    FrameStats RendererCore::RenderFrame(const FrameInput& input)
    {
        ++m_frame;
        m_device->BeginFrame(m_frame);
        FrameSlot& slot = m_slots[m_frame % m_slots.size()];
        FrameStats stats;
        stats.items = static_cast<std::uint32_t>(input.items.size());

        int width = 0, height = 0;
        m_device->GetBackbufferSize(width, height);
        if (width <= 0 || height <= 0)
        {
            return stats;
        }

        // Set 0: the frame.
        FrameData frame;
        const Math::Vec3 lightDirection = input.hasLight ? glm::normalize(input.lightDirection) : Math::Vec3(0, -1, 0);
        const float li = input.hasLight ? input.lightIntensity : 0.0f;
        frame.lightDirection = Math::Vec4(lightDirection, 0);
        frame.lightColour = Math::Vec4(input.lightColour.r * li, input.lightColour.g * li, input.lightColour.b * li, 1);
        frame.ambient = Math::Vec4(input.ambient.r * input.ambientIntensity, input.ambient.g * input.ambientIntensity,
                                   input.ambient.b * input.ambientIntensity, 1);
        frame.time = Math::Vec4(input.time, 0, 0, 0);
        m_device->WriteBuffer(slot.frameUbo, 0, Bytes(frame));

        // Set 3: every item's matrices, shared by every view; an item's index is its draw's first instance.
        std::vector<Math::Mat4> matrices;
        matrices.reserve(input.items.size() * 2);
        for (const DrawItemInput& item : input.items)
        {
            matrices.push_back(item.world);
            matrices.push_back(glm::transpose(glm::inverse(item.world)));
        }
        const std::size_t drawBytes = std::max<std::size_t>(input.items.size(), 1) * kDrawItemSize;
        if (drawBytes > slot.drawCapacity)
        {
            if (slot.draws.IsValid())
            {
                m_device->Destroy(slot.draws);
                m_device->Destroy(slot.drawGroup);
            }
            slot.drawCapacity = std::max(drawBytes, slot.drawCapacity * 2);
            slot.draws = m_device->CreateBuffer(
                {slot.drawCapacity, static_cast<std::uint32_t>(Gpu::BufferUsage::Storage), "draw items"});
            slot.drawGroup = m_device->CreateBindGroup({m_drawLayout, {{0, slot.draws, 0, slot.drawCapacity}}});
        }
        m_device->WriteBuffer(slot.draws, 0, Bytes(matrices));

        // Debug lines: one vertex buffer per frame slot.
        const std::size_t lineBytes = input.lines.size() * sizeof(DebugLineVertex);
        if (lineBytes > slot.lineCapacity)
        {
            if (slot.lines.IsValid())
                m_device->Destroy(slot.lines);
            slot.lineCapacity = std::max(lineBytes, slot.lineCapacity * 2);
            slot.lines = m_device->CreateBuffer(
                {slot.lineCapacity, static_cast<std::uint32_t>(Gpu::BufferUsage::Vertex), "debug lines"});
        }
        if (lineBytes > 0)
            m_device->WriteBuffer(slot.lines, 0, std::as_bytes(input.lines));

        const GpuImage* skyImage = nullptr;
        if (auto it = m_images.find(input.skyCubemap); input.skyCubemap != 0 && it != m_images.end() && it->second.cube)
            skyImage = &it->second;

        // The draw list: sort keys with the stage in the high bits, then pipeline, material and mesh for opaque
        // items; back-to-front depth for transparent ones (per view, below).
        struct Draw
        {
            std::uint64_t key;
            std::uint32_t item;
            const GpuMesh* mesh;
            const GpuMaterial* material;
            Gpu::PipelineHandle pipeline;
        };
        std::vector<Draw> opaque, transparent;
        for (std::uint32_t i = 0; i < input.items.size(); ++i)
        {
            const DrawItemInput& item = input.items[i];
            auto mesh = m_meshes.find(item.mesh);
            if (mesh == m_meshes.end() || mesh->second.indexCount == 0)
                continue; // empty or stale: skipped (ADR-0023)
            const GpuMaterial* material = &m_fallback;
            if (auto m = m_materials.find(item.material); item.material != 0 && m != m_materials.end())
                material = &m->second;
            else
                LogOnce("an item has no material, or a stale one: drawn with the fallback");
            const Gpu::PipelineHandle pipeline =
                MeshPipeline(material->program, material->blend, material->doubleSided);
            const std::uint64_t key = (std::uint64_t{material->blend == BlendMode::Opaque ? 0u : 1u} << 60) |
                                      (std::uint64_t{pipeline.index & 0xFFFF} << 32) |
                                      (std::uint64_t{material->group.index & 0xFFFF} << 16) |
                                      std::uint64_t{mesh->second.vertices.index & 0xFFFF};
            (material->blend == BlendMode::Opaque ? opaque : transparent)
                .push_back({key, i, &mesh->second, material, pipeline});
        }
        std::sort(opaque.begin(), opaque.end(), [](const Draw& a, const Draw& b) { return a.key < b.key; });

        RenderGraph graph;
        const GraphTexture backbuffer = graph.ImportBackbuffer(width, height);
        std::vector<Gpu::BindGroupHandle> frameGroups; // made this frame, destroyed (deferred) at its end
        const std::size_t viewCount = std::min(input.views.size(), kMaxViews);

        for (std::size_t v = 0; v < viewCount; ++v)
        {
            const ViewInput& view = input.views[v];
            const int vx = static_cast<int>(view.viewport[0] * static_cast<float>(width));
            const int vy = static_cast<int>(view.viewport[1] * static_cast<float>(height));
            const int vw = std::max(1, static_cast<int>(view.viewport[2] * static_cast<float>(width)));
            const int vh = std::max(1, static_cast<int>(view.viewport[3] * static_cast<float>(height)));

            ViewData data;
            data.view = view.view;
            data.projection = view.projection;
            data.viewProjection = view.projection * view.view;
            data.inverseViewProjection = glm::inverse(data.viewProjection);
            data.cameraPosition = Math::Vec4(view.position, 1);
            data.viewport = Math::Vec4(static_cast<float>(vx), static_cast<float>(vy), static_cast<float>(vw),
                                       static_cast<float>(vh));
            m_device->WriteBuffer(slot.viewUbo, v * kViewSlotStride, Bytes(data));
            const Gpu::BindGroupHandle viewGroup = slot.viewGroups[v];

            const std::string suffix = " (view " + std::to_string(v) + ")";
            const GraphTexture colour =
                graph.CreateTransient("SceneColor" + suffix, {Gpu::TextureFormat::Rgba16Float, vw, vh});
            const GraphTexture depth =
                graph.CreateTransient("SceneDepth" + suffix, {Gpu::TextureFormat::Depth32Float, vw, vh});
            const Colour clear = view.hasClearColour ? view.clearColour : input.skyColour;

            // Record functions run later, inside Execute: capture by value.
            auto pass = [colour, depth, clear, vw, vh](PassContext& ctx, const char* label, bool clearTargets) {
                Gpu::PassDesc d;
                d.colour = ctx.texture(colour);
                d.depth = ctx.texture(depth);
                d.colourLoad = clearTargets ? Gpu::LoadOp::Clear : Gpu::LoadOp::Load;
                d.depthLoad = clearTargets ? Gpu::LoadOp::Clear : Gpu::LoadOp::Load;
                d.clearColour = {clear.r, clear.g, clear.b, 1};
                d.depthReadOnly = !clearTargets;
                d.width = vw;
                d.height = vh;
                d.label = label;
                return d;
            };

            auto drawItems = [&slot, viewGroup, this](Gpu::PassEncoder& enc, const std::vector<Draw>& draws) {
                Gpu::PipelineHandle current;
                for (const Draw& d : draws)
                {
                    if (!(d.pipeline == current))
                    {
                        enc.SetPipeline(d.pipeline);
                        enc.SetBindGroup(0, slot.frameGroup);
                        enc.SetBindGroup(1, viewGroup);
                        enc.SetBindGroup(3, slot.drawGroup);
                        current = d.pipeline;
                    }
                    enc.SetBindGroup(2, d.material->group);
                    enc.SetVertexBuffer(d.mesh->vertices);
                    enc.SetIndexBuffer(d.mesh->indices);
                    enc.DrawIndexed(d.mesh->indexCount, 0, 0, d.item);
                }
                (void)this;
            };

            graph.AddPass(
                "Opaque" + suffix, RenderStage::Opaque,
                [&](PassBuilder& b) {
                    b.Write(colour, Gpu::Usage::ColourAttachment, false);
                    b.Write(depth, Gpu::Usage::DepthAttachment, false);
                },
                [=, &opaque](PassContext& ctx) {
                    Gpu::PassEncoder enc(ctx.list, pass(ctx, "Opaque", true));
                    drawItems(enc, opaque);
                });

            if (skyImage != nullptr && !view.hasClearColour)
            {
                Gpu::BindGroupHandle skyGroup = m_device->CreateBindGroup(
                    {m_programs[ProgramIndex("sky")].sets[2], {{0, {}, 0, 0, skyImage->texture, m_clampSampler}}});
                frameGroups.push_back(skyGroup);
                graph.AddPass(
                    "Background" + suffix, RenderStage::Background,
                    [&](PassBuilder& b) {
                        b.Write(colour, Gpu::Usage::ColourAttachment, true);
                        b.Read(depth, Gpu::Usage::DepthReadOnly);
                    },
                    [=, this](PassContext& ctx) {
                        Gpu::PassEncoder enc(ctx.list, pass(ctx, "Background", false));
                        enc.SetPipeline(m_skyPipeline);
                        enc.SetBindGroup(1, viewGroup);
                        enc.SetBindGroup(2, skyGroup);
                        enc.Draw(3);
                    });
            }

            if (!transparent.empty())
            {
                // Back to front by the view-space depth of each item's world origin (ADR-0019).
                std::vector<Draw> sorted = transparent;
                std::sort(sorted.begin(), sorted.end(), [&](const Draw& a, const Draw& b) {
                    const float za = (view.view * input.items[a.item].world[3]).z;
                    const float zb = (view.view * input.items[b.item].world[3]).z;
                    return za < zb;
                });
                graph.AddPass(
                    "Transparent" + suffix, RenderStage::Transparent,
                    [&](PassBuilder& b) {
                        b.Write(colour, Gpu::Usage::ColourAttachment, true);
                        b.Read(depth, Gpu::Usage::DepthReadOnly);
                    },
                    [=](PassContext& ctx) {
                        Gpu::PassEncoder enc(ctx.list, pass(ctx, "Transparent", false));
                        drawItems(enc, sorted);
                    });
            }

            if (view.drawDebug && !input.lines.empty())
            {
                const auto lineCount = static_cast<std::uint32_t>(input.lines.size());
                graph.AddPass(
                    "Debug" + suffix, RenderStage::Debug,
                    [&](PassBuilder& b) {
                        b.Write(colour, Gpu::Usage::ColourAttachment, true);
                        b.Read(depth, Gpu::Usage::DepthReadOnly);
                    },
                    [=, this, &slot](PassContext& ctx) {
                        Gpu::PassEncoder enc(ctx.list, pass(ctx, "Debug", false));
                        enc.SetPipeline(m_linePipeline);
                        enc.SetBindGroup(1, viewGroup);
                        enc.SetVertexBuffer(slot.lines);
                        enc.Draw(lineCount);
                    });
            }

            // A pass nothing reads, to show culling: it writes a transient no one uses.
            const GraphTexture unused =
                graph.CreateTransient("Unused" + suffix, {Gpu::TextureFormat::Rgba16Float, 4, 4});
            graph.AddPass(
                "Unused" + suffix, RenderStage::PostProcess,
                [&](PassBuilder& b) { b.Write(unused, Gpu::Usage::ColourAttachment, false); }, [](PassContext&) {});

            const bool first = v == 0;
            graph.AddPass(
                "Output" + suffix, RenderStage::Output,
                [&](PassBuilder& b) {
                    b.Read(colour, Gpu::Usage::Sampled);
                    b.Write(backbuffer, Gpu::Usage::ColourAttachment, !first);
                },
                [=, this, &frameGroups](PassContext& ctx) {
                    Gpu::BindGroupHandle group =
                        m_device->CreateBindGroup({m_programs[ProgramIndex("output")].sets[2],
                                                   {{0, {}, 0, 0, ctx.texture(colour), m_clampSampler}}});
                    frameGroups.push_back(group);
                    Gpu::PassDesc d;
                    d.colourLoad = first ? Gpu::LoadOp::Clear : Gpu::LoadOp::Load;
                    d.clearColour = {0, 0, 0, 1};
                    d.sampled = {{ctx.texture(colour), Gpu::Usage::Sampled}};
                    d.width = width;
                    d.height = height;
                    d.label = "Output";
                    Gpu::PassEncoder enc(ctx.list, d);
                    enc.SetViewport(vx, vy, vw, vh);
                    enc.SetPipeline(m_outputPipeline);
                    enc.SetBindGroup(2, group);
                    enc.Draw(3);
                });
        }

        const RenderGraph::Result result = graph.Execute(*m_device, m_transients, m_frame);
        for (const std::string& error : result.errors)
            LogOnce("render graph: " + error);
        for (Gpu::BindGroupHandle group : frameGroups)
            m_device->Destroy(group);
        m_transients.EndFrame(*m_device, m_frame);
        m_device->Present(); // the renderer's last step (ADR-0022)

        const Gpu::DeviceStats device = m_device->Stats();
        stats.draws = device.draws;
        stats.passes = device.passes;
        stats.pipelines = device.pipelines;
        stats.liveBuffers = device.liveBuffers;
        stats.liveTextures = device.liveTextures;
        stats.culledPasses = result.passesCulled;
        stats.passOrder = result.order;
        stats.uploads = m_uploads;
        m_uploads = 0;
        return stats;
    }

    bool RendererCore::SetSwapInterval(int interval)
    {
        return m_device->SetSwapInterval(interval);
    }

    std::vector<std::uint8_t> RendererCore::ReadBackbuffer(int& width, int& height)
    {
        return m_device->ReadBackbuffer(width, height);
    }
} // namespace Fenrir::Render
