#include "Backend/OpenGL/OpenGLDevice.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "FenrirAssets/ShaderGlBindings.hpp"
#include "FenrirLogger/ILogger.hpp"

namespace Fenrir::Graphics
{
    namespace
    {
        constexpr int RequiredMajor = 4;
        constexpr int RequiredMinor = 6;

        /** @brief Index of an enum value in the tables below, which list each enum's values in its order. */
        template <typename E>
        constexpr std::size_t IndexOf(E value) noexcept
        {
            return static_cast<std::size_t>(value);
        }

        /** @brief GL's internal format for each TextureFormat. */
        constexpr std::array<GLenum, 4> internalFormats{GL_RGBA8, GL_SRGB8_ALPHA8, GL_RGBA16F, GL_DEPTH_COMPONENT32F};

        /** @brief How WriteTexture's tightly packed texels of a format are laid out for GL. */
        struct UploadFormat
        {
            GLenum format = GL_RGBA;
            GLenum type = GL_UNSIGNED_BYTE;
            std::size_t bytesPerTexel = 4;
        };

        /** @brief The upload layout of each TextureFormat. */
        constexpr std::array<UploadFormat, 4> uploadFormats{{
            {GL_RGBA, GL_UNSIGNED_BYTE, 4},
            {GL_RGBA, GL_UNSIGNED_BYTE, 4},
            {GL_RGBA, GL_HALF_FLOAT, 8},
            {GL_DEPTH_COMPONENT, GL_FLOAT, 4},
        }};

        /** @brief GL's wrap mode for each AddressMode. */
        constexpr std::array<GLint, 3> wrapModes{GL_REPEAT, GL_MIRRORED_REPEAT, GL_CLAMP_TO_EDGE};

        GLint MinFilterOf(const SamplerDesc& desc)
        {
            const bool linear = desc.minFilter == Filter::Linear;
            if (desc.mipFilter == Filter::Linear)
            {
                return linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
            }
            return linear ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
        }

        /** @brief GL's depth function for each CompareOp. */
        constexpr std::array<GLenum, 5> compareFunctions{GL_ALWAYS, GL_LESS, GL_LEQUAL, GL_GREATER, GL_GEQUAL};

        /** @brief A vertex attribute format as GL's component count, type and normalisation. */
        struct AttributeFormat
        {
            GLint size = 3;
            GLenum type = GL_FLOAT;
            GLboolean normalised = GL_FALSE;
        };

        /** @brief The GL attribute format of each VertexFormat. */
        constexpr std::array<AttributeFormat, 5> attributeFormats{{
            {1, GL_FLOAT, GL_FALSE},
            {2, GL_FLOAT, GL_FALSE},
            {3, GL_FLOAT, GL_FALSE},
            {4, GL_FLOAT, GL_FALSE},
            {4, GL_UNSIGNED_BYTE, GL_TRUE},
        }};

        /** @brief A blend mode's source and destination factors, for colour then alpha; `enabled` false for None. */
        struct BlendFactors
        {
            bool enabled = false;
            GLenum sourceColour = GL_ONE;
            GLenum destinationColour = GL_ZERO;
            GLenum sourceAlpha = GL_ONE;
            GLenum destinationAlpha = GL_ZERO;
        };

        /** @brief The factors of each BlendMode. */
        constexpr std::array<BlendFactors, 4> blendFactors{{
            {false, GL_ONE, GL_ZERO, GL_ONE, GL_ZERO},
            {true, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA},
            {true, GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA},
            {true, GL_ONE, GL_ONE, GL_ONE, GL_ONE},
        }};

        /** @brief Each Vsync mode's name, for the log. */
        constexpr std::array<std::string_view, 3> vsyncNames{"Off", "On", "Adaptive"};

        /** @brief The surface's swap interval for each Vsync mode: 0 off, 1 on, -1 adaptive. */
        constexpr std::array<int, 3> swapIntervals{0, 1, -1};

        std::string_view SeverityName(GLenum severity)
        {
            switch (severity)
            {
            case GL_DEBUG_SEVERITY_HIGH:
                return "high";
            case GL_DEBUG_SEVERITY_MEDIUM:
                return "medium";
            case GL_DEBUG_SEVERITY_LOW:
                return "low";
            default:
                return "notification";
            }
        }

        /**
         * @brief GL's debug callback, synchronous so it runs inside the GL call that caused it (ADR-0016's debug
         * validation): GL errors and high-severity messages are errors, the rest warnings or information.
         */
        void GLAD_API_PTR OnDebugMessage(GLenum, GLenum type, GLuint, GLenum severity, GLsizei length,
                                         const GLchar* message, const void* user)
        {
            auto* logger = static_cast<ILogger*>(const_cast<void*>(user));
            const std::string_view text =
                length < 0 ? std::string_view(message) : std::string_view(message, static_cast<std::size_t>(length));
            const std::string_view level = SeverityName(severity);
            if (type == GL_DEBUG_TYPE_ERROR || severity == GL_DEBUG_SEVERITY_HIGH)
            {
                logger->Error("OpenGL debug message ({}): {}", level, text);
            }
            else if (severity == GL_DEBUG_SEVERITY_MEDIUM)
            {
                logger->Warn("OpenGL debug message ({}): {}", level, text);
            }
            else
            {
                logger->Info("OpenGL debug message ({}): {}", level, text);
            }
        }

        /** @brief glad's loader, reading each GL function through the graphics surface. */
        GLADapiproc LoadThroughSurface(void* surface, const char* name)
        {
            void* address = static_cast<GraphicsSurface*>(surface)->GetProcAddress(name);
            GLADapiproc function = nullptr;
            static_assert(sizeof(function) == sizeof(address));
            // A function address arrives as a neutral void* (ADR-0022); copying its bytes avoids a cast between object
            // and function pointers.
            std::memcpy(&function, &address, sizeof(function));
            return function;
        }

        /** @brief A shader stage's GL SPIR-V from a variant; null if it has none. */
        const std::vector<std::uint32_t>* GlSpirvOf(const ShaderVariant& variant, ShaderStage stage)
        {
            for (const ShaderStageCode& code : variant.stages)
            {
                if (code.stage == stage && !code.glSpirv.empty())
                {
                    return &code.glSpirv;
                }
            }
            return nullptr;
        }
    } // namespace

    OpenGLDevice::OpenGLDevice(GraphicsSurface& surface, const DeviceConfig& config, ILogger& logger)
        : m_surface(&surface), m_config(config), m_logger(&logger), m_gl(std::make_unique<GladGLContext>())
    {
        if (m_config.framesInFlight == 0)
        {
            m_config.framesInFlight = 1;
        }
    }

    OpenGLDevice::~OpenGLDevice()
    {
        if (!m_started)
        {
            return;
        }
        m_surface->MakeCurrent();
        GladGLContext& gl = *m_gl;
        m_buffers.ForEachHeld([&gl](GlBuffer& buffer) { gl.DeleteBuffers(1, &buffer.name); });
        m_textures.ForEachHeld([&gl](GlTexture& texture) {
            if (texture.name != 0)
            {
                gl.DeleteTextures(1, &texture.name);
            }
        });
        m_samplers.ForEachHeld([&gl](std::uint32_t& sampler) { gl.DeleteSamplers(1, &sampler); });
        m_pipelines.ForEachHeld([&gl](GlPipeline& pipeline) { gl.DeleteVertexArrays(1, &pipeline.vertexArray); });
        for (auto& [key, program] : m_programs)
        {
            gl.DeleteProgram(program.name);
        }
        for (auto& [attachments, framebuffer] : m_framebuffers)
        {
            gl.DeleteFramebuffers(1, &framebuffer);
        }
        if (m_config.debugValidation)
        {
            gl.DebugMessageCallback(nullptr, nullptr);
        }
    }

    bool OpenGLDevice::Start(std::string& diagnostic)
    {
        if (!m_surface->MakeCurrent())
        {
            diagnostic = "the OpenGL backend could not make the graphics surface's context current";
            return false;
        }
        const int version = gladLoadGLContextUserPtr(m_gl.get(), LoadThroughSurface, m_surface);
        if (version == 0)
        {
            diagnostic = "the OpenGL backend could not load OpenGL through the graphics surface: it has no OpenGL "
                         "context";
            return false;
        }
        const int major = GLAD_VERSION_MAJOR(version);
        const int minor = GLAD_VERSION_MINOR(version);
        GladGLContext& gl = *m_gl;
        GLint profile = 0;
        if (major > 3)
        {
            gl.GetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
        }
        if (major < RequiredMajor || (major == RequiredMajor && minor < RequiredMinor) ||
            (profile & GL_CONTEXT_CORE_PROFILE_BIT) == 0)
        {
            const auto* renderer = reinterpret_cast<const char*>(gl.GetString(GL_RENDERER));
            diagnostic = "the OpenGL backend needs an OpenGL 4.6 core context, but the graphics surface's is " +
                         std::to_string(major) + "." + std::to_string(minor) +
                         ((profile & GL_CONTEXT_CORE_PROFILE_BIT) != 0 ? " core" : "") + " on " +
                         (renderer != nullptr ? renderer : "an unknown renderer");
            return false;
        }
        m_started = true;

        if (m_config.debugValidation)
        {
            gl.Enable(GL_DEBUG_OUTPUT);
            // Synchronous, so a message arrives inside the GL call that caused it, on this thread.
            gl.Enable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            gl.DebugMessageCallback(OnDebugMessage, m_logger);
            // Notifications (buffer placement, the backend's own debug groups) are noise in a log.
            gl.DebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
        }
        // The device's conventions (ADR-0016, ADR-0026): [0, 1] depth, and sRGB targets encode what is written to
        // them. The clip origin is set per pass.
        gl.ClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE);
        gl.Enable(GL_FRAMEBUFFER_SRGB);
        gl.Enable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
        gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl.PixelStorei(GL_PACK_ALIGNMENT, 1);

        const PixelSize size = m_surface->GetPixelSize();
        TextureDesc backbuffer;
        backbuffer.format = TextureFormat::Rgba8Srgb;
        backbuffer.width = size.width;
        backbuffer.height = size.height;
        backbuffer.usage = TextureUsage::ColourAttachment;
        // GL name 0: the default framebuffer, the window's.
        m_backbuffer = m_textures.Add(GlTexture{0, backbuffer});

        const auto* versionText = reinterpret_cast<const char*>(gl.GetString(GL_VERSION));
        const auto* renderer = reinterpret_cast<const char*>(gl.GetString(GL_RENDERER));
        const std::string_view versionView = versionText != nullptr ? versionText : "";
        const std::string_view rendererView = renderer != nullptr ? renderer : "";
        m_logger->Info("OpenGL backend started: OpenGL {} on {}", versionView, rendererView);
        return true;
    }

    std::uint32_t OpenGLDevice::FramesInFlight() const
    {
        return m_config.framesInFlight;
    }

    void OpenGLDevice::BeginFrame(std::uint64_t frameIndex)
    {
        m_frameIndex = frameIndex;
        GladGLContext& gl = *m_gl;
        const std::uint32_t frames = m_config.framesInFlight;
        m_buffers.Collect(frameIndex, frames, [&gl](GlBuffer& buffer) { gl.DeleteBuffers(1, &buffer.name); });
        m_textures.Collect(frameIndex, frames, [this, &gl](GlTexture& texture) {
            ForgetFramebuffers(texture.name);
            gl.DeleteTextures(1, &texture.name);
        });
        m_samplers.Collect(frameIndex, frames, [&gl](std::uint32_t& sampler) { gl.DeleteSamplers(1, &sampler); });
        m_layouts.Collect(frameIndex, frames, [](BindingGroupLayoutDesc&) {});
        m_groups.Collect(frameIndex, frames, [](GlBindingGroup&) {});
        m_pipelines.Collect(frameIndex, frames, [this](GlPipeline& pipeline) { ReleasePipeline(pipeline); });
    }

    BufferHandle OpenGLDevice::CreateBuffer(const BufferDesc& desc, std::span<const std::byte> initial,
                                            std::string_view label)
    {
        GladGLContext& gl = *m_gl;
        GlBuffer buffer;
        buffer.size = desc.size;
        gl.CreateBuffers(1, &buffer.name);
        gl.NamedBufferStorage(buffer.name, static_cast<GLsizeiptr>(desc.size), nullptr, GL_DYNAMIC_STORAGE_BIT);
        if (!initial.empty())
        {
            gl.NamedBufferSubData(buffer.name, 0, static_cast<GLsizeiptr>(initial.size()), initial.data());
        }
        if (!label.empty())
        {
            gl.ObjectLabel(GL_BUFFER, buffer.name, static_cast<GLsizei>(label.size()), label.data());
        }
        return m_buffers.Add(buffer);
    }

    TextureHandle OpenGLDevice::CreateTexture(const TextureDesc& desc, std::string_view label)
    {
        GladGLContext& gl = *m_gl;
        GlTexture texture;
        texture.desc = desc;
        const auto width = static_cast<GLsizei>(desc.width);
        const auto height = static_cast<GLsizei>(desc.height);
        if (desc.samples > 1)
        {
            gl.CreateTextures(GL_TEXTURE_2D_MULTISAMPLE, 1, &texture.name);
            gl.TextureStorage2DMultisample(texture.name, static_cast<GLsizei>(desc.samples),
                                           internalFormats.at(IndexOf(desc.format)), width, height, GL_TRUE);
        }
        else
        {
            gl.CreateTextures(desc.kind == TextureKind::Cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D, 1, &texture.name);
            gl.TextureStorage2D(texture.name, static_cast<GLsizei>(desc.mipLevels),
                                internalFormats.at(IndexOf(desc.format)), width, height);
        }
        if (!label.empty())
        {
            gl.ObjectLabel(GL_TEXTURE, texture.name, static_cast<GLsizei>(label.size()), label.data());
        }
        return m_textures.Add(texture);
    }

    SamplerHandle OpenGLDevice::CreateSampler(const SamplerDesc& desc, std::string_view label)
    {
        GladGLContext& gl = *m_gl;
        GLuint sampler = 0;
        gl.CreateSamplers(1, &sampler);
        gl.SamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, MinFilterOf(desc));
        gl.SamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, desc.magFilter == Filter::Linear ? GL_LINEAR : GL_NEAREST);
        gl.SamplerParameteri(sampler, GL_TEXTURE_WRAP_S, wrapModes.at(IndexOf(desc.addressU)));
        gl.SamplerParameteri(sampler, GL_TEXTURE_WRAP_T, wrapModes.at(IndexOf(desc.addressV)));
        gl.SamplerParameteri(sampler, GL_TEXTURE_WRAP_R, wrapModes.at(IndexOf(desc.addressW)));
        if (desc.maxAnisotropy > 1.0f)
        {
            gl.SamplerParameterf(sampler, GL_TEXTURE_MAX_ANISOTROPY, desc.maxAnisotropy);
        }
        if (!label.empty())
        {
            gl.ObjectLabel(GL_SAMPLER, sampler, static_cast<GLsizei>(label.size()), label.data());
        }
        return m_samplers.Add(sampler);
    }

    BindingGroupLayoutHandle OpenGLDevice::CreateBindingGroupLayout(const BindingGroupLayoutDesc& desc,
                                                                    std::string_view)
    {
        // GL has no layout objects: binding groups read their entries' kinds from it.
        return m_layouts.Add(desc);
    }

    BindingGroupHandle OpenGLDevice::CreateBindingGroup(const BindingGroupDesc& desc, std::string_view)
    {
        const BindingGroupLayoutDesc* layout = m_layouts.Find(desc.layout);
        if (layout == nullptr)
        {
            m_logger->Error("OpenGL backend: a binding group was made with a stale binding-group layout");
            return {};
        }
        GlBindingGroup group;
        group.entries = desc.entries;
        for (const BindingGroupEntry& entry : desc.entries)
        {
            const auto found = std::ranges::find(layout->entries, entry.binding, &BindingLayoutEntry::binding);
            if (found == layout->entries.end())
            {
                m_logger->Error("OpenGL backend: a binding group sets binding {}, which its layout does not declare",
                                entry.binding);
                return {};
            }
            group.kinds.push_back(found->kind);
        }
        return m_groups.Add(std::move(group));
    }

    PipelineResult OpenGLDevice::CreatePipeline(const PipelineDesc& desc, std::string_view label)
    {
        if (desc.shader == nullptr)
        {
            return {{}, "the pipeline names no shader variant"};
        }
        const std::vector<std::uint32_t>* vertex = GlSpirvOf(*desc.shader, ShaderStage::Vertex);
        const std::vector<std::uint32_t>* fragment = GlSpirvOf(*desc.shader, ShaderStage::Fragment);
        if (vertex == nullptr || fragment == nullptr)
        {
            return {{}, "the shader variant lacks the GL SPIR-V of a vertex or a fragment stage"};
        }

        GladGLContext& gl = *m_gl;
        ProgramKey key{*vertex, *fragment};
        auto program = m_programs.find(key);
        if (program == m_programs.end())
        {
            std::string diagnostic;
            std::array<GLuint, 2> shaders{};
            const std::array<std::pair<GLenum, const std::vector<std::uint32_t>*>, 2> stages{
                {{GL_VERTEX_SHADER, vertex}, {GL_FRAGMENT_SHADER, fragment}}};
            for (std::size_t i = 0; i < stages.size() && diagnostic.empty(); ++i)
            {
                const auto& [type, spirv] = stages[i];
                shaders[i] = gl.CreateShader(type);
                gl.ShaderBinary(1, &shaders[i], GL_SHADER_BINARY_FORMAT_SPIR_V, spirv->data(),
                                static_cast<GLsizei>(spirv->size() * sizeof(std::uint32_t)));
                gl.SpecializeShader(shaders[i], "main", 0, nullptr, nullptr);
                GLint compiled = 0;
                gl.GetShaderiv(shaders[i], GL_COMPILE_STATUS, &compiled);
                if (compiled == 0)
                {
                    std::array<GLchar, 2048> log{};
                    gl.GetShaderInfoLog(shaders[i], static_cast<GLsizei>(log.size()), nullptr, log.data());
                    diagnostic = std::string(i == 0 ? "the vertex" : "the fragment") +
                                 " stage could not be specialised: " + log.data();
                }
            }
            GLuint name = 0;
            if (diagnostic.empty())
            {
                name = gl.CreateProgram();
                gl.AttachShader(name, shaders[0]);
                gl.AttachShader(name, shaders[1]);
                gl.LinkProgram(name);
                gl.DetachShader(name, shaders[0]);
                gl.DetachShader(name, shaders[1]);
                GLint linked = 0;
                gl.GetProgramiv(name, GL_LINK_STATUS, &linked);
                if (linked == 0)
                {
                    std::array<GLchar, 2048> log{};
                    gl.GetProgramInfoLog(name, static_cast<GLsizei>(log.size()), nullptr, log.data());
                    diagnostic = std::string("the program could not be linked: ") + log.data();
                    gl.DeleteProgram(name);
                }
            }
            for (const GLuint shader : shaders)
            {
                if (shader != 0)
                {
                    gl.DeleteShader(shader);
                }
            }
            if (!diagnostic.empty())
            {
                return {{}, std::move(diagnostic)};
            }
            if (!label.empty())
            {
                gl.ObjectLabel(GL_PROGRAM, name, static_cast<GLsizei>(label.size()), label.data());
            }
            program = m_programs.emplace(std::move(key), GlProgram{name, 0}).first;
        }
        ++program->second.users;

        GlPipeline pipeline;
        pipeline.program = &program->first;
        pipeline.programName = program->second.name;
        pipeline.topology = desc.topology;
        pipeline.cull = desc.cull;
        pipeline.depth = desc.depth;
        pipeline.blend = desc.blend;
        gl.CreateVertexArrays(1, &pipeline.vertexArray);
        for (std::size_t slot = 0; slot < desc.vertexBuffers.size(); ++slot)
        {
            const VertexBufferLayout& layout = desc.vertexBuffers[slot];
            pipeline.strides.push_back(layout.stride);
            for (const VertexAttribute& attribute : layout.attributes)
            {
                const AttributeFormat format = attributeFormats.at(IndexOf(attribute.format));
                gl.EnableVertexArrayAttrib(pipeline.vertexArray, attribute.location);
                gl.VertexArrayAttribFormat(pipeline.vertexArray, attribute.location, format.size, format.type,
                                           format.normalised, attribute.offset);
                gl.VertexArrayAttribBinding(pipeline.vertexArray, attribute.location, static_cast<GLuint>(slot));
            }
        }
        if (!label.empty())
        {
            gl.ObjectLabel(GL_VERTEX_ARRAY, pipeline.vertexArray, static_cast<GLsizei>(label.size()), label.data());
        }
        return {m_pipelines.Add(std::move(pipeline)), {}};
    }

    bool OpenGLDevice::WriteBuffer(BufferHandle buffer, std::size_t offset, std::span<const std::byte> bytes)
    {
        const GlBuffer* found = m_buffers.Find(buffer);
        if (found == nullptr)
        {
            return false;
        }
        if (!bytes.empty())
        {
            m_gl->NamedBufferSubData(found->name, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(bytes.size()),
                                     bytes.data());
        }
        return true;
    }

    bool OpenGLDevice::WriteTexture(TextureHandle texture, std::uint32_t mipLevel, std::uint32_t layer,
                                    std::span<const std::byte> texels)
    {
        const GlTexture* found = m_textures.Find(texture);
        if (found == nullptr)
        {
            return false;
        }
        const TextureDesc& desc = found->desc;
        const std::uint32_t layers = desc.kind == TextureKind::Cube ? 6 : 1;
        const std::uint32_t width = std::max(1u, desc.width >> mipLevel);
        const std::uint32_t height = std::max(1u, desc.height >> mipLevel);
        const UploadFormat upload = uploadFormats.at(IndexOf(desc.format));
        const std::size_t expected = std::size_t{width} * height * upload.bytesPerTexel;
        if (found->name == 0 || desc.samples > 1 || mipLevel >= desc.mipLevels || layer >= layers ||
            texels.size() != expected)
        {
            m_logger->Error("OpenGL backend: WriteTexture refused {} bytes for mip {} of layer {} of a {}x{} texture "
                            "({} expected; the backbuffer and multisampled textures cannot be written)",
                            texels.size(), mipLevel, layer, desc.width, desc.height, expected);
            return false;
        }
        GladGLContext& gl = *m_gl;
        if (desc.kind == TextureKind::Cube)
        {
            gl.TextureSubImage3D(found->name, static_cast<GLint>(mipLevel), 0, 0, static_cast<GLint>(layer),
                                 static_cast<GLsizei>(width), static_cast<GLsizei>(height), 1, upload.format,
                                 upload.type, texels.data());
        }
        else
        {
            gl.TextureSubImage2D(found->name, static_cast<GLint>(mipLevel), 0, 0, static_cast<GLsizei>(width),
                                 static_cast<GLsizei>(height), upload.format, upload.type, texels.data());
        }
        return true;
    }

    bool OpenGLDevice::Destroy(BufferHandle buffer)
    {
        return m_buffers.Destroy(buffer, m_frameIndex);
    }

    bool OpenGLDevice::Destroy(TextureHandle texture)
    {
        // The backbuffer is the surface's, not the caller's.
        return texture != m_backbuffer && m_textures.Destroy(texture, m_frameIndex);
    }

    bool OpenGLDevice::Destroy(SamplerHandle sampler)
    {
        return m_samplers.Destroy(sampler, m_frameIndex);
    }

    bool OpenGLDevice::Destroy(BindingGroupLayoutHandle layout)
    {
        return m_layouts.Destroy(layout, m_frameIndex);
    }

    bool OpenGLDevice::Destroy(BindingGroupHandle group)
    {
        return m_groups.Destroy(group, m_frameIndex);
    }

    bool OpenGLDevice::Destroy(PipelineHandle pipeline)
    {
        return m_pipelines.Destroy(pipeline, m_frameIndex);
    }

    bool OpenGLDevice::IsLive(BufferHandle buffer) const
    {
        return m_buffers.IsLive(buffer);
    }

    bool OpenGLDevice::IsLive(TextureHandle texture) const
    {
        return m_textures.IsLive(texture);
    }

    bool OpenGLDevice::IsLive(SamplerHandle sampler) const
    {
        return m_samplers.IsLive(sampler);
    }

    bool OpenGLDevice::IsLive(BindingGroupLayoutHandle layout) const
    {
        return m_layouts.IsLive(layout);
    }

    bool OpenGLDevice::IsLive(BindingGroupHandle group) const
    {
        return m_groups.IsLive(group);
    }

    bool OpenGLDevice::IsLive(PipelineHandle pipeline) const
    {
        return m_pipelines.IsLive(pipeline);
    }

    TextureHandle OpenGLDevice::Backbuffer() const
    {
        return m_backbuffer;
    }

    void OpenGLDevice::Submit(const CommandList& list)
    {
        for (const Command& command : list.commands)
        {
            std::visit(
                [this](const auto& cmd) {
                    // Inside a skipped pass only its end is replayed.
                    if (!m_replay.skipping || std::is_same_v<std::decay_t<decltype(cmd)>, CmdEndPass>)
                    {
                        Execute(cmd);
                    }
                },
                command);
        }
    }

    void OpenGLDevice::Present()
    {
        m_surface->Present();
    }

    void OpenGLDevice::WaitIdle()
    {
        m_gl->Finish();
    }

    Vsync OpenGLDevice::SetVsync(Vsync mode)
    {
        if (m_surface->SetSwapInterval(swapIntervals.at(IndexOf(mode))))
        {
            return mode;
        }
        const std::string_view refused = vsyncNames.at(IndexOf(mode));
        if (mode != Vsync::On && m_surface->SetSwapInterval(swapIntervals.at(IndexOf(Vsync::On))))
        {
            m_logger->Warn("OpenGL backend: the graphics surface does not support vsync {}, so it is On", refused);
        }
        else
        {
            m_logger->Warn("OpenGL backend: the graphics surface refused vsync {}, and On as well", refused);
        }
        return Vsync::On;
    }

    void OpenGLDevice::Execute(const CmdBeginPass& command)
    {
        const PassDesc& pass = command.pass;
        m_replay = ReplayState{};
        m_replay.drawingToBackbuffer = pass.colour.size() == 1 && pass.colour.front().texture == m_backbuffer;

        GLuint framebuffer = 0;
        PixelSize size;
        if (m_replay.drawingToBackbuffer)
        {
            size = m_surface->GetPixelSize();
        }
        else
        {
            for (const ColourAttachment& colour : pass.colour)
            {
                const GlTexture* texture = m_textures.Find(colour.texture);
                if (texture == nullptr || texture->name == 0)
                {
                    m_logger->Error("OpenGL backend: pass '{}' draws into a stale texture or mixes in the backbuffer, "
                                    "so it is skipped",
                                    pass.label);
                    m_replay.skipping = true;
                    return;
                }
                size = {texture->desc.width, texture->desc.height};
            }
            if (pass.depth)
            {
                const GlTexture* texture = m_textures.Find(pass.depth->texture);
                if (texture == nullptr || texture->name == 0)
                {
                    m_logger->Error("OpenGL backend: pass '{}' has a stale depth attachment, so it is skipped",
                                    pass.label);
                    m_replay.skipping = true;
                    return;
                }
                size = {texture->desc.width, texture->desc.height};
            }
            framebuffer = FramebufferFor(pass);
        }
        m_replay.targetHeight = size.height;

        GladGLContext& gl = *m_gl;
        gl.PushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, static_cast<GLsizei>(pass.label.size()), pass.label.data());
        // Upper-left puts NDC +y at row 0, so offscreen targets are stored top row first, as uploaded images are; the
        // window's row 0 is its bottom, so a pass drawing to it goes back to lower-left. GL keeps counter-clockwise
        // front faces under both (spike lesson 3).
        gl.ClipControl(m_replay.drawingToBackbuffer ? GL_LOWER_LEFT : GL_UPPER_LEFT, GL_ZERO_TO_ONE);
        gl.BindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        gl.Viewport(0, 0, static_cast<GLsizei>(size.width), static_cast<GLsizei>(size.height));
        gl.Disable(GL_SCISSOR_TEST);
        gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        for (std::size_t i = 0; i < pass.colour.size(); ++i)
        {
            if (pass.colour[i].load == LoadOp::Clear)
            {
                gl.ClearNamedFramebufferfv(framebuffer, GL_COLOR, static_cast<GLint>(i), pass.colour[i].clear.data());
            }
        }
        if (pass.depth && pass.depth->load == LoadOp::Clear)
        {
            gl.DepthMask(GL_TRUE);
            gl.ClearNamedFramebufferfv(framebuffer, GL_DEPTH, 0, &pass.depth->clear);
        }
    }

    void OpenGLDevice::Execute(const CmdEndPass&)
    {
        if (m_replay.skipping)
        {
            m_replay.skipping = false;
            return;
        }
        m_gl->PopDebugGroup();
        m_replay = ReplayState{};
    }

    void OpenGLDevice::Execute(const CmdSetPipeline& command)
    {
        const GlPipeline* pipeline = m_pipelines.Find(command.pipeline);
        m_replay.pipeline = pipeline;
        if (pipeline == nullptr)
        {
            m_logger->Error("OpenGL backend: a stale pipeline was set; draws are skipped until another is");
            return;
        }
        GladGLContext& gl = *m_gl;
        gl.UseProgram(pipeline->programName);
        gl.BindVertexArray(pipeline->vertexArray);
        BindVertexInput();

        if (pipeline->cull == CullMode::Back)
        {
            gl.Enable(GL_CULL_FACE);
            gl.CullFace(GL_BACK);
        }
        else
        {
            gl.Disable(GL_CULL_FACE);
        }
        gl.FrontFace(GL_CCW);

        if (pipeline->depth.test)
        {
            gl.Enable(GL_DEPTH_TEST);
            gl.DepthFunc(compareFunctions.at(IndexOf(pipeline->depth.compare)));
        }
        else
        {
            gl.Disable(GL_DEPTH_TEST);
        }
        gl.DepthMask(pipeline->depth.write ? GL_TRUE : GL_FALSE);

        const BlendFactors& blend = blendFactors.at(IndexOf(pipeline->blend));
        if (blend.enabled)
        {
            gl.Enable(GL_BLEND);
            gl.BlendFuncSeparate(blend.sourceColour, blend.destinationColour, blend.sourceAlpha,
                                 blend.destinationAlpha);
        }
        else
        {
            gl.Disable(GL_BLEND);
        }
    }

    void OpenGLDevice::Execute(const CmdSetBindingGroup& command)
    {
        const GlBindingGroup* group = m_groups.Find(command.group);
        if (group == nullptr || command.set >= BindingSetCount)
        {
            m_logger->Error("OpenGL backend: a stale binding group, or one for set {} (of {}), was bound", command.set,
                            BindingSetCount);
            return;
        }
        GladGLContext& gl = *m_gl;
        for (std::size_t i = 0; i < group->entries.size(); ++i)
        {
            const BindingGroupEntry& entry = group->entries[i];
            // As FenrirShaderCompiler flattened the GL copy's bindings (spike lesson 2).
            const GLuint unit = GlBindingOf(command.set, entry.binding);
            if (group->kinds[i] == ShaderBindingKind::Texture)
            {
                const GlTexture* texture = m_textures.Find(entry.texture);
                const std::uint32_t* sampler = m_samplers.Find(entry.sampler);
                gl.BindTextureUnit(unit, texture != nullptr ? texture->name : 0);
                gl.BindSampler(unit, sampler != nullptr ? *sampler : 0);
                continue;
            }
            const GlBuffer* buffer = m_buffers.Find(entry.buffer);
            if (buffer == nullptr)
            {
                m_logger->Error("OpenGL backend: binding {} of set {} names a stale buffer", entry.binding,
                                command.set);
                continue;
            }
            const std::size_t size = entry.size != 0 ? entry.size : buffer->size - entry.offset;
            gl.BindBufferRange(group->kinds[i] == ShaderBindingKind::UniformBuffer ? GL_UNIFORM_BUFFER
                                                                                   : GL_SHADER_STORAGE_BUFFER,
                               unit, buffer->name, static_cast<GLintptr>(entry.offset), static_cast<GLsizeiptr>(size));
        }
    }

    void OpenGLDevice::Execute(const CmdSetVertexBuffer& command)
    {
        const GlBuffer* buffer = m_buffers.Find(command.buffer);
        if (buffer == nullptr)
        {
            m_logger->Error("OpenGL backend: vertex buffer slot {} was given a stale buffer", command.slot);
            return;
        }
        if (m_replay.vertexBuffers.size() <= command.slot)
        {
            m_replay.vertexBuffers.resize(command.slot + 1);
        }
        m_replay.vertexBuffers[command.slot] = {buffer->name, command.offset};
        BindVertexInput();
    }

    void OpenGLDevice::Execute(const CmdSetIndexBuffer& command)
    {
        const GlBuffer* buffer = m_buffers.Find(command.buffer);
        if (buffer == nullptr)
        {
            m_logger->Error("OpenGL backend: a stale index buffer was bound");
            return;
        }
        m_replay.indexBuffer = {buffer->name, command.offset};
        m_replay.indexFormat = command.format;
        BindVertexInput();
    }

    void OpenGLDevice::Execute(const CmdSetViewport& command)
    {
        m_gl->Viewport(command.rect.x, WindowY(command.rect), static_cast<GLsizei>(command.rect.width),
                       static_cast<GLsizei>(command.rect.height));
    }

    void OpenGLDevice::Execute(const CmdSetScissor& command)
    {
        m_gl->Enable(GL_SCISSOR_TEST);
        m_gl->Scissor(command.rect.x, WindowY(command.rect), static_cast<GLsizei>(command.rect.width),
                      static_cast<GLsizei>(command.rect.height));
    }

    void OpenGLDevice::Execute(const CmdDraw& command)
    {
        if (m_replay.pipeline == nullptr)
        {
            return;
        }
        m_gl->DrawArraysInstancedBaseInstance(m_replay.pipeline->topology == Topology::Lines ? GL_LINES : GL_TRIANGLES,
                                              static_cast<GLint>(command.firstVertex),
                                              static_cast<GLsizei>(command.vertexCount),
                                              static_cast<GLsizei>(command.instanceCount), command.firstInstance);
    }

    void OpenGLDevice::Execute(const CmdDrawIndexed& command)
    {
        if (m_replay.pipeline == nullptr)
        {
            return;
        }
        const bool shortIndices = m_replay.indexFormat == IndexFormat::UInt16;
        const std::size_t offset =
            m_replay.indexBuffer.offset + std::size_t{command.firstIndex} * (shortIndices ? 2u : 4u);
        m_gl->DrawElementsInstancedBaseVertexBaseInstance(
            m_replay.pipeline->topology == Topology::Lines ? GL_LINES : GL_TRIANGLES,
            static_cast<GLsizei>(command.indexCount), shortIndices ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
            reinterpret_cast<const void*>(offset), static_cast<GLsizei>(command.instanceCount), command.baseVertex,
            command.firstInstance);
    }

    void OpenGLDevice::Execute(const CmdDispatch&)
    {
        m_logger->Error("OpenGL backend: a dispatch was submitted, but pipelines are graphics only so far");
    }

    void OpenGLDevice::Execute(const CmdCopyBuffer& command)
    {
        const GlBuffer* source = m_buffers.Find(command.source);
        const GlBuffer* destination = m_buffers.Find(command.destination);
        if (source == nullptr || destination == nullptr)
        {
            m_logger->Error("OpenGL backend: a buffer copy names a stale buffer");
            return;
        }
        m_gl->CopyNamedBufferSubData(source->name, destination->name, static_cast<GLintptr>(command.sourceOffset),
                                     static_cast<GLintptr>(command.destinationOffset),
                                     static_cast<GLsizeiptr>(command.size));
    }

    void OpenGLDevice::BindVertexInput()
    {
        const GlPipeline* pipeline = m_replay.pipeline;
        if (pipeline == nullptr)
        {
            return;
        }
        GladGLContext& gl = *m_gl;
        const std::size_t slots = std::min(pipeline->strides.size(), m_replay.vertexBuffers.size());
        for (std::size_t slot = 0; slot < slots; ++slot)
        {
            const BoundBuffer& bound = m_replay.vertexBuffers[slot];
            if (bound.name != 0)
            {
                gl.VertexArrayVertexBuffer(pipeline->vertexArray, static_cast<GLuint>(slot), bound.name,
                                           static_cast<GLintptr>(bound.offset),
                                           static_cast<GLsizei>(pipeline->strides[slot]));
            }
        }
        gl.VertexArrayElementBuffer(pipeline->vertexArray, m_replay.indexBuffer.name);
    }

    std::uint32_t OpenGLDevice::FramebufferFor(const PassDesc& pass)
    {
        std::vector<std::uint32_t> names;
        for (const ColourAttachment& colour : pass.colour)
        {
            names.push_back(m_textures.Find(colour.texture)->name);
        }
        names.push_back(pass.depth ? m_textures.Find(pass.depth->texture)->name : 0);
        if (const auto found = m_framebuffers.find(names); found != m_framebuffers.end())
        {
            return found->second;
        }

        GladGLContext& gl = *m_gl;
        GLuint framebuffer = 0;
        gl.CreateFramebuffers(1, &framebuffer);
        std::vector<GLenum> drawBuffers;
        for (std::size_t i = 0; i + 1 < names.size(); ++i)
        {
            const auto attachment = static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i);
            gl.NamedFramebufferTexture(framebuffer, attachment, names[i], 0);
            drawBuffers.push_back(attachment);
        }
        if (names.back() != 0)
        {
            gl.NamedFramebufferTexture(framebuffer, GL_DEPTH_ATTACHMENT, names.back(), 0);
        }
        if (drawBuffers.empty())
        {
            gl.NamedFramebufferDrawBuffer(framebuffer, GL_NONE);
        }
        else
        {
            gl.NamedFramebufferDrawBuffers(framebuffer, static_cast<GLsizei>(drawBuffers.size()), drawBuffers.data());
        }
        if (gl.CheckNamedFramebufferStatus(framebuffer, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            m_logger->Error("OpenGL backend: pass '{}' draws into attachments GL cannot draw into together",
                            pass.label);
        }
        m_framebuffers.emplace(std::move(names), framebuffer);
        return framebuffer;
    }

    void OpenGLDevice::ForgetFramebuffers(std::uint32_t name)
    {
        for (auto it = m_framebuffers.begin(); it != m_framebuffers.end();)
        {
            if (std::ranges::find(it->first, name) != it->first.end())
            {
                m_gl->DeleteFramebuffers(1, &it->second);
                it = m_framebuffers.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void OpenGLDevice::ReleasePipeline(GlPipeline& pipeline)
    {
        m_gl->DeleteVertexArrays(1, &pipeline.vertexArray);
        const auto program = m_programs.find(*pipeline.program);
        if (program != m_programs.end() && --program->second.users == 0)
        {
            m_gl->DeleteProgram(program->second.name);
            m_programs.erase(program);
        }
    }

    std::int32_t OpenGLDevice::WindowY(const PixelRect& rect) const
    {
        // Pixel rectangles count from the top; the window's rows count from its bottom.
        if (m_replay.drawingToBackbuffer)
        {
            return static_cast<std::int32_t>(m_replay.targetHeight) - rect.y - static_cast<std::int32_t>(rect.height);
        }
        return rect.y;
    }

    OpenGLDevice::Texels OpenGLDevice::ReadTexels(TextureHandle texture)
    {
        const GlTexture* found = m_textures.Find(texture);
        if (found == nullptr || found->name == 0 || found->desc.samples > 1 ||
            found->desc.kind != TextureKind::Texture2D || found->desc.format == TextureFormat::Depth32Float)
        {
            return {};
        }
        Texels texels;
        texels.width = found->desc.width;
        texels.height = found->desc.height;
        texels.rgba.resize(std::size_t{texels.width} * texels.height * 4);
        // GL returns memory row 0 first, which an upper-left clip origin made the top row.
        m_gl->GetTextureImage(found->name, 0, GL_RGBA, GL_FLOAT,
                              static_cast<GLsizei>(texels.rgba.size() * sizeof(float)), texels.rgba.data());
        return texels;
    }

    std::uint32_t OpenGLDevice::GlName(BufferHandle buffer)
    {
        const GlBuffer* found = m_buffers.Find(buffer);
        return found != nullptr ? found->name : 0;
    }

    std::uint32_t OpenGLDevice::GlName(TextureHandle texture)
    {
        const GlTexture* found = m_textures.Find(texture);
        return found != nullptr ? found->name : 0;
    }

    bool OpenGLDevice::IsGlBuffer(std::uint32_t name) const
    {
        return m_gl->IsBuffer(name) == GL_TRUE;
    }

    bool OpenGLDevice::IsGlTexture(std::uint32_t name) const
    {
        return m_gl->IsTexture(name) == GL_TRUE;
    }

    OpenGLDeviceResult CreateOpenGLDevice(GraphicsSurface& surface, const DeviceConfig& config, ILogger& logger)
    {
        std::unique_ptr<OpenGLDevice> device(new OpenGLDevice(surface, config, logger));
        std::string diagnostic;
        if (!device->Start(diagnostic))
        {
            return {nullptr, std::move(diagnostic)};
        }
        return {std::move(device), {}};
    }
} // namespace Fenrir::Graphics
