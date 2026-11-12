#include "Backend/OpenGL/OpenGLDevice.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <tuple>
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
        /** @brief The SPIR-V extension glslang names for a vertex stage writing gl_Layer. */
        constexpr std::string_view LayerExtension = "SPV_EXT_shader_viewport_index_layer";
        /** @brief NVIDIA's name for the same capability, all its GL drivers list. */
        constexpr std::string_view NvLayerExtension = "SPV_NV_viewport_array2";

        /** @brief A SPIR-V literal string: its bytes, nul-terminated, packed four to a word, little end first. */
        std::vector<std::uint32_t> LiteralWords(std::string_view text)
        {
            std::vector<std::uint32_t> words(text.size() / 4 + 1, 0u);
            for (std::size_t i = 0; i < text.size(); ++i)
            {
                words[i / 4] |= std::uint32_t{static_cast<unsigned char>(text[i])} << (8 * (i % 4));
            }
            return words;
        }

        /**
         * @brief `spirv` with its OpExtension `from` naming `to` instead; empty when it has no such instruction. The
         * instruction's word count follows the new name's length.
         */
        std::vector<std::uint32_t> WithExtensionRenamed(const std::vector<std::uint32_t>& spirv, std::string_view from,
                                                        std::string_view to)
        {
            constexpr std::uint32_t OpExtension = 10;
            constexpr std::size_t HeaderWords = 5;
            const std::vector<std::uint32_t> wanted = LiteralWords(from);
            std::size_t at = HeaderWords;
            while (at < spirv.size())
            {
                const std::uint32_t count = spirv[at] >> 16;
                if (count == 0 || at + count > spirv.size())
                {
                    return {};
                }
                if ((spirv[at] & 0xFFFFu) == OpExtension && count - 1 == wanted.size() &&
                    std::equal(wanted.begin(), wanted.end(), spirv.begin() + static_cast<std::ptrdiff_t>(at) + 1))
                {
                    const std::vector<std::uint32_t> name = LiteralWords(to);
                    std::vector<std::uint32_t> renamed(spirv.begin(), spirv.begin() + static_cast<std::ptrdiff_t>(at));
                    renamed.push_back((static_cast<std::uint32_t>(name.size() + 1) << 16) | OpExtension);
                    renamed.insert(renamed.end(), name.begin(), name.end());
                    renamed.insert(renamed.end(), spirv.begin() + static_cast<std::ptrdiff_t>(at + count), spirv.end());
                    return renamed;
                }
                at += count;
            }
            return {};
        }

        /** @brief A texture's layers: a cube's six faces, six for each of a cube array's cubes, else one. */
        std::uint32_t LayersOf(const TextureDesc& desc) noexcept
        {
            switch (desc.kind)
            {
            case TextureKind::Cube:
                return 6;
            case TextureKind::CubeArray:
                return 6 * std::max(desc.layers, 1u);
            default:
                return 1;
            }
        }

        constexpr std::array<GLenum, 9> internalFormats{GL_RGBA8,   GL_SRGB8_ALPHA8, GL_RGBA16F, GL_DEPTH_COMPONENT32F,
                                                        GL_RGBA32F, GL_R8,           GL_RG16F,   GL_R32F,
                                                        GL_RG32F};

        /** @brief How WriteTexture's tightly packed texels of a format are laid out for GL. */
        struct UploadFormat
        {
            GLenum format = GL_RGBA;
            GLenum type = GL_UNSIGNED_BYTE;
            std::size_t bytesPerTexel = 4;
        };

        /** @brief The upload layout of each TextureFormat. */
        constexpr std::array<UploadFormat, 9> uploadFormats{{
            {GL_RGBA, GL_UNSIGNED_BYTE, 4},
            {GL_RGBA, GL_UNSIGNED_BYTE, 4},
            {GL_RGBA, GL_HALF_FLOAT, 8},
            {GL_DEPTH_COMPONENT, GL_FLOAT, 4},
            {GL_RGBA, GL_FLOAT, 16},
            {GL_RED, GL_UNSIGNED_BYTE, 1},
            {GL_RG, GL_HALF_FLOAT, 4},
            {GL_RED, GL_FLOAT, 4},
            {GL_RG, GL_FLOAT, 8},
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
        constexpr std::array<GLenum, 6> compareFunctions{GL_ALWAYS,  GL_LESS,   GL_LEQUAL,
                                                         GL_GREATER, GL_GEQUAL, GL_EQUAL};

        /** @brief A vertex attribute format as GL's component count, type and normalisation. */
        struct AttributeFormat
        {
            GLint size = 3;
            GLenum type = GL_FLOAT;
            GLboolean normalised = GL_FALSE;
            /** @brief Read as integers, not converted to floats: set up with glVertexArrayAttribIFormat. */
            bool integer = false;
        };

        /** @brief The GL attribute format of each VertexFormat. */
        constexpr std::array<AttributeFormat, 7> attributeFormats{{
            {1, GL_FLOAT, GL_FALSE},
            {2, GL_FLOAT, GL_FALSE},
            {3, GL_FLOAT, GL_FALSE},
            {4, GL_FLOAT, GL_FALSE},
            {4, GL_UNSIGNED_BYTE, GL_TRUE},
            {4, GL_UNSIGNED_SHORT, GL_FALSE, true},
            {4, GL_UNSIGNED_SHORT, GL_TRUE},
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
            {true, GL_ONE, GL_ONE, GL_ZERO, GL_ONE},
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

        /** @brief GL's primitive mode for a pipeline's topology. */
        constexpr GLenum PrimitiveOf(Topology topology) noexcept
        {
            return topology == Topology::Lines ? GL_LINES : GL_TRIANGLES;
        }

        /** @brief GL's index type for an index format. */
        constexpr GLenum IndexTypeOf(IndexFormat format) noexcept
        {
            return format == IndexFormat::UInt16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
        }

        /**
         * @brief The memory barrier bits a read of a buffer written as storage by a shader needs, by how a pass
         * declares the read; 0 for a use that is no such read.
         */
        constexpr GLbitfield BarrierBitsOf(ResourceUsage usage) noexcept
        {
            switch (usage)
            {
            case ResourceUsage::StorageRead:
            case ResourceUsage::StorageWrite:
                return GL_SHADER_STORAGE_BARRIER_BIT;
            case ResourceUsage::VertexInput:
                return GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT | GL_ELEMENT_ARRAY_BARRIER_BIT;
            case ResourceUsage::IndirectRead:
                return GL_COMMAND_BARRIER_BIT;
            case ResourceUsage::CopySource:
            case ResourceUsage::CopyDestination:
                return GL_BUFFER_UPDATE_BARRIER_BIT;
            default:
                return 0;
            }
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
        // A vertex stage writing gl_Layer, as point shadows' instanced faces do, needs the driver to take it in SPIR-V:
        // glslang names SPV_EXT_shader_viewport_index_layer, and NVIDIA's drivers list only SPV_NV_viewport_array2,
        // whose capability is the same one, so a stage naming the first is renamed to the second for them.
        GLint spirvExtensions = 0;
        gl.GetIntegerv(GL_NUM_SPIR_V_EXTENSIONS, &spirvExtensions);
        for (GLint i = 0; i < spirvExtensions; ++i)
        {
            const auto* name =
                reinterpret_cast<const char*>(gl.GetStringi(GL_SPIR_V_EXTENSIONS, static_cast<GLuint>(i)));
            const std::string_view extension = name != nullptr ? name : "";
            if (extension == LayerExtension)
            {
                m_layerExtension = LayerExtension;
            }
            else if (extension == NvLayerExtension && m_layerExtension.empty())
            {
                m_layerExtension = NvLayerExtension;
            }
        }
        m_features.vertexLayer = !m_layerExtension.empty();

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
        m_logger->Info("OpenGL backend started: OpenGL {} on {}{}", versionView, rendererView,
                       m_features.vertexLayer ? ", vertex stages writing gl_Layer" : "");
        return true;
    }

    std::uint32_t OpenGLDevice::FramesInFlight() const
    {
        return m_config.framesInFlight;
    }

    DeviceFeatures OpenGLDevice::Features() const
    {
        return m_features;
    }

    void OpenGLDevice::BeginFrame(std::uint64_t frameIndex)
    {
        m_frameIndex = frameIndex;
        GladGLContext& gl = *m_gl;
        const std::uint32_t frames = m_config.framesInFlight;
        m_buffers.Collect(frameIndex, frames, [this, &gl](GlBuffer& buffer) {
            m_storageWritten.erase(buffer.name);
            gl.DeleteBuffers(1, &buffer.name);
        });
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
        buffer.usage = desc.usage;
        buffer.writtenFrame = m_frameIndex;
        gl.CreateBuffers(1, &buffer.name);
        // A buffer the CPU reads back is a hint to keep it where the client can read it.
        const GLbitfield flags =
            GL_DYNAMIC_STORAGE_BIT | (HasUsage(desc.usage, BufferUsage::Readback) ? GL_CLIENT_STORAGE_BIT : 0);
        gl.NamedBufferStorage(buffer.name, static_cast<GLsizeiptr>(desc.size), nullptr, flags);
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
        else if (desc.kind == TextureKind::CubeArray)
        {
            // Six layers a cube.
            gl.CreateTextures(GL_TEXTURE_CUBE_MAP_ARRAY, 1, &texture.name);
            gl.TextureStorage3D(texture.name, static_cast<GLsizei>(desc.mipLevels),
                                internalFormats.at(IndexOf(desc.format)), width, height,
                                static_cast<GLsizei>(LayersOf(desc)));
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
            // Clamped to what the device has (GL 4.6 guarantees at least 16).
            GLfloat most = 1.0f;
            gl.GetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &most);
            gl.SamplerParameterf(sampler, GL_TEXTURE_MAX_ANISOTROPY, std::min(desc.maxAnisotropy, most));
        }
        if (desc.compare.has_value())
        {
            gl.SamplerParameteri(sampler, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
            gl.SamplerParameteri(sampler, GL_TEXTURE_COMPARE_FUNC,
                                 static_cast<GLint>(compareFunctions.at(IndexOf(*desc.compare))));
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
            group.indicesInKind.push_back(
                static_cast<std::uint32_t>(std::ranges::count_if(layout->entries, [&](const BindingLayoutEntry& other) {
                    return other.kind == found->kind && other.binding < entry.binding;
                })));
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

        std::string diagnostic;
        const auto program = ProgramFor({*vertex, *fragment, {}}, label, diagnostic);
        if (program == m_programs.end())
        {
            return {{}, std::move(diagnostic)};
        }

        GladGLContext& gl = *m_gl;
        GlPipeline pipeline;
        pipeline.program = &program->first;
        pipeline.programName = program->second.name;
        pipeline.topology = desc.topology;
        pipeline.cull = desc.cull;
        pipeline.depth = desc.depth;
        pipeline.blend = desc.blend;
        pipeline.encodeSrgb = desc.encodeSrgb;
        gl.CreateVertexArrays(1, &pipeline.vertexArray);
        for (std::size_t slot = 0; slot < desc.vertexBuffers.size(); ++slot)
        {
            const VertexBufferLayout& layout = desc.vertexBuffers[slot];
            pipeline.strides.push_back(layout.stride);
            for (const VertexAttribute& attribute : layout.attributes)
            {
                const AttributeFormat format = attributeFormats.at(IndexOf(attribute.format));
                gl.EnableVertexArrayAttrib(pipeline.vertexArray, attribute.location);
                if (format.integer)
                {
                    gl.VertexArrayAttribIFormat(pipeline.vertexArray, attribute.location, format.size, format.type,
                                                attribute.offset);
                }
                else
                {
                    gl.VertexArrayAttribFormat(pipeline.vertexArray, attribute.location, format.size, format.type,
                                               format.normalised, attribute.offset);
                }
                gl.VertexArrayAttribBinding(pipeline.vertexArray, attribute.location, static_cast<GLuint>(slot));
            }
        }
        if (!label.empty())
        {
            gl.ObjectLabel(GL_VERTEX_ARRAY, pipeline.vertexArray, static_cast<GLsizei>(label.size()), label.data());
        }
        return {m_pipelines.Add(std::move(pipeline)), {}};
    }

    PipelineResult OpenGLDevice::CreateComputePipeline(const ComputePipelineDesc& desc, std::string_view label)
    {
        if (desc.shader == nullptr)
        {
            return {{}, "the pipeline names no shader variant"};
        }
        const std::vector<std::uint32_t>* compute = GlSpirvOf(*desc.shader, ShaderStage::Compute);
        if (compute == nullptr)
        {
            return {{}, "the shader variant lacks the GL SPIR-V of a compute stage"};
        }
        std::string diagnostic;
        const auto program = ProgramFor({{}, {}, *compute}, label, diagnostic);
        if (program == m_programs.end())
        {
            return {{}, std::move(diagnostic)};
        }
        GlPipeline pipeline;
        pipeline.program = &program->first;
        pipeline.programName = program->second.name;
        pipeline.compute = true;
        return {m_pipelines.Add(std::move(pipeline)), {}};
    }

    std::map<OpenGLDevice::ProgramKey, OpenGLDevice::GlProgram>::iterator OpenGLDevice::ProgramFor(
        ProgramKey key, std::string_view label, std::string& diagnostic)
    {
        auto program = m_programs.find(key);
        if (program != m_programs.end())
        {
            ++program->second.users;
            return program;
        }
        GladGLContext& gl = *m_gl;
        // A graphics program's vertex and fragment stages, or a compute program's one stage.
        std::vector<std::tuple<GLenum, const std::vector<std::uint32_t>*, const char*>> stages;
        if (key.compute.empty())
        {
            stages = {{GL_VERTEX_SHADER, &key.vertex, "the vertex"},
                      {GL_FRAGMENT_SHADER, &key.fragment, "the fragment"}};
        }
        else
        {
            stages = {{GL_COMPUTE_SHADER, &key.compute, "the compute"}};
        }
        std::vector<GLuint> shaders;
        for (const auto& [type, written, name] : stages)
        {
            // The vertex-layer extension under the name the driver knows, where that is not glslang's.
            std::vector<std::uint32_t> renamed;
            if (!m_layerExtension.empty() && m_layerExtension != LayerExtension)
            {
                renamed = WithExtensionRenamed(*written, LayerExtension, m_layerExtension);
            }
            const std::vector<std::uint32_t>* spirv = renamed.empty() ? written : &renamed;
            const GLuint shader = shaders.emplace_back(gl.CreateShader(type));
            gl.ShaderBinary(1, &shader, GL_SHADER_BINARY_FORMAT_SPIR_V, spirv->data(),
                            static_cast<GLsizei>(spirv->size() * sizeof(std::uint32_t)));
            gl.SpecializeShader(shader, "main", 0, nullptr, nullptr);
            GLint compiled = 0;
            gl.GetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
            if (compiled == 0)
            {
                std::array<GLchar, 2048> log{};
                gl.GetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
                diagnostic = std::string(name) + " stage could not be specialised: " + log.data();
                break;
            }
        }
        GLuint name = 0;
        if (diagnostic.empty())
        {
            name = gl.CreateProgram();
            for (const GLuint shader : shaders)
            {
                gl.AttachShader(name, shader);
            }
            gl.LinkProgram(name);
            for (const GLuint shader : shaders)
            {
                gl.DetachShader(name, shader);
            }
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
            gl.DeleteShader(shader);
        }
        if (!diagnostic.empty())
        {
            return m_programs.end();
        }
        if (!label.empty())
        {
            gl.ObjectLabel(GL_PROGRAM, name, static_cast<GLsizei>(label.size()), label.data());
        }
        return m_programs.emplace(std::move(key), GlProgram{name, 1}).first;
    }

    bool OpenGLDevice::WriteBuffer(BufferHandle buffer, std::size_t offset, std::span<const std::byte> bytes)
    {
        GlBuffer* found = m_buffers.Find(buffer);
        if (found == nullptr)
        {
            return false;
        }
        if (!bytes.empty())
        {
            // A buffer update after a shader wrote the buffer needs the same barrier a copy does.
            const auto written = m_storageWritten.find(found->name);
            if (written != m_storageWritten.end())
            {
                MakeVisible(GL_BUFFER_UPDATE_BARRIER_BIT & ~written->second);
            }
            m_gl->NamedBufferSubData(found->name, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(bytes.size()),
                                     bytes.data());
        }
        found->writtenFrame = m_frameIndex;
        return true;
    }

    bool OpenGLDevice::ReadBuffer(BufferHandle buffer, std::size_t offset, std::span<std::byte> bytes)
    {
        const GlBuffer* found = m_buffers.Find(buffer);
        if (found == nullptr || !HasUsage(found->usage, BufferUsage::Readback) || offset + bytes.size() > found->size)
        {
            return false;
        }
        // Refused until the writing frame has left flight: GL would otherwise wait for its commands here.
        if (!HasLeftFlight(found->writtenFrame, m_config.framesInFlight, m_frameIndex))
        {
            return false;
        }
        if (!bytes.empty())
        {
            // A read after a shader wrote the buffer needs the same barrier a copy does; a copy's write needs none.
            const auto written = m_storageWritten.find(found->name);
            if (written != m_storageWritten.end())
            {
                MakeVisible(GL_BUFFER_UPDATE_BARRIER_BIT & ~written->second);
            }
            m_gl->GetNamedBufferSubData(found->name, static_cast<GLintptr>(offset),
                                        static_cast<GLsizeiptr>(bytes.size()), bytes.data());
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
        const std::uint32_t layers = LayersOf(desc);
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
        if (desc.kind != TextureKind::Texture2D)
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
        BarrierBefore(pass);
        // What the pass writes as storage, later passes read as vertices, storage, indirect commands or a copy's
        // source, which GL makes visible only after a barrier before them: those passes' uses say which.
        for (const PassResourceUse& use : pass.uses)
        {
            GlBuffer* buffer = BufferOf(use);
            if (buffer != nullptr && use.usage == ResourceUsage::StorageWrite)
            {
                m_replay.storageWrites.push_back(buffer->name);
                buffer->writtenFrame = m_frameIndex;
            }
        }
        if (pass.queue == QueueKind::Compute)
        {
            // Dispatches only: no target to bind.
            m_replay.computing = true;
            m_gl->PushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, static_cast<GLsizei>(pass.label.size()),
                                 pass.label.data());
            return;
        }
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
                GlTexture* texture = m_textures.Find(colour.texture);
                if (texture == nullptr || texture->name == 0)
                {
                    m_logger->Error("OpenGL backend: pass '{}' draws into a stale texture or mixes in the backbuffer, "
                                    "so it is skipped",
                                    pass.label);
                    m_replay.skipping = true;
                    return;
                }
                // Drawn into, a texture spans every level again, so the level drawn is inside its range; a binding of
                // one level in this pass narrows it again before the draw.
                SampleLevels(*texture, std::nullopt);
                size = {std::max(texture->desc.width >> colour.mipLevel, 1u),
                        std::max(texture->desc.height >> colour.mipLevel, 1u)};
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
        else if (pass.depth)
        {
            // A pass that tests a depth buffer drawn by an earlier pass, with other views' passes between them:
            // NVIDIA's early depth test can then take a stale picture of the buffer (the cleared one) and pass
            // fragments behind what is stored. An empty clear, scissored to nothing, makes the driver take the buffer
            // up again; it changes no pixel.
            gl.Enable(GL_SCISSOR_TEST);
            gl.Scissor(0, 0, 0, 0);
            gl.DepthMask(GL_TRUE);
            gl.Clear(GL_DEPTH_BUFFER_BIT);
            gl.Disable(GL_SCISSOR_TEST);
        }
    }

    void OpenGLDevice::Execute(const CmdEndPass&)
    {
        if (m_replay.skipping)
        {
            m_replay.skipping = false;
            return;
        }
        // Written again, a buffer's earlier barriers no longer cover it.
        for (const std::uint32_t name : m_replay.storageWrites)
        {
            m_storageWritten[name] = 0;
        }
        // A pipeline on the gamma path turned encoding off; every pass, and its clears, starts with it on.
        m_gl->Enable(GL_FRAMEBUFFER_SRGB);
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
        if (pipeline->compute != m_replay.computing)
        {
            m_logger->Error("OpenGL backend: a {} pipeline was set in a {} pass; it runs nothing",
                            pipeline->compute ? "compute" : "graphics", m_replay.computing ? "compute" : "graphics");
            m_replay.pipeline = nullptr;
            return;
        }
        if (pipeline->compute)
        {
            return;
        }
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
        if (pipeline->encodeSrgb)
        {
            gl.Enable(GL_FRAMEBUFFER_SRGB);
        }
        else
        {
            gl.Disable(GL_FRAMEBUFFER_SRGB);
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
            const GLuint unit = GlBindingOf(command.set, group->indicesInKind[i]);
            if (group->kinds[i] == ShaderBindingKind::Texture)
            {
                GlTexture* texture = m_textures.Find(entry.texture);
                const std::uint32_t* sampler = m_samplers.Find(entry.sampler);
                // A binding of one level narrows the texture's levels to it, so drawing into another level is no
                // feedback loop; the next binding of every level widens them again.
                if (texture != nullptr && texture->name != 0)
                {
                    SampleLevels(*texture, entry.mipLevel);
                }
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
        if (m_replay.pipeline == nullptr || m_replay.pipeline->compute)
        {
            return;
        }
        m_gl->DrawArraysInstancedBaseInstance(PrimitiveOf(m_replay.pipeline->topology),
                                              static_cast<GLint>(command.firstVertex),
                                              static_cast<GLsizei>(command.vertexCount),
                                              static_cast<GLsizei>(command.instanceCount), command.firstInstance);
    }

    void OpenGLDevice::Execute(const CmdDrawIndexed& command)
    {
        if (m_replay.pipeline == nullptr || m_replay.pipeline->compute)
        {
            return;
        }
        const bool shortIndices = m_replay.indexFormat == IndexFormat::UInt16;
        const std::size_t offset =
            m_replay.indexBuffer.offset + std::size_t{command.firstIndex} * (shortIndices ? 2u : 4u);
        m_gl->DrawElementsInstancedBaseVertexBaseInstance(
            PrimitiveOf(m_replay.pipeline->topology), static_cast<GLsizei>(command.indexCount),
            IndexTypeOf(m_replay.indexFormat), reinterpret_cast<const void*>(offset),
            static_cast<GLsizei>(command.instanceCount), command.baseVertex, command.firstInstance);
    }

    void OpenGLDevice::Execute(const CmdDrawIndexedIndirect& command)
    {
        if (m_replay.pipeline == nullptr || m_replay.pipeline->compute)
        {
            return;
        }
        const GlBuffer* buffer = m_buffers.Find(command.buffer);
        if (buffer == nullptr)
        {
            m_logger->Error("OpenGL backend: an indirect draw reads a stale command buffer, so it is skipped");
            return;
        }
        // Each command's firstIndex counts indices from the bound index buffer's offset, as CmdDrawIndexed's does, but
        // the commands are the GPU's to read and GL counts from the element buffer's start, so only offset 0 can be
        // honoured; the null backend reports the same.
        if (m_replay.indexBuffer.offset != 0)
        {
            m_logger->Error("OpenGL backend: an indirect draw's index buffer is bound {} bytes in, which indirect "
                            "commands cannot count from, so it is skipped",
                            m_replay.indexBuffer.offset);
            return;
        }
        GladGLContext& gl = *m_gl;
        gl.BindBuffer(GL_DRAW_INDIRECT_BUFFER, buffer->name);
        gl.MultiDrawElementsIndirect(PrimitiveOf(m_replay.pipeline->topology), IndexTypeOf(m_replay.indexFormat),
                                     reinterpret_cast<const void*>(command.offset),
                                     static_cast<GLsizei>(command.drawCount), static_cast<GLsizei>(command.stride));
    }

    void OpenGLDevice::Execute(const CmdDispatch& command)
    {
        if (m_replay.pipeline == nullptr || !m_replay.pipeline->compute)
        {
            return;
        }
        m_gl->DispatchCompute(command.groupsX, command.groupsY, command.groupsZ);
    }

    void OpenGLDevice::Execute(const CmdCopyBuffer& command)
    {
        const GlBuffer* source = m_buffers.Find(command.source);
        GlBuffer* destination = m_buffers.Find(command.destination);
        if (source == nullptr || destination == nullptr)
        {
            m_logger->Error("OpenGL backend: a buffer copy names a stale buffer");
            return;
        }
        m_gl->CopyNamedBufferSubData(source->name, destination->name, static_cast<GLintptr>(command.sourceOffset),
                                     static_cast<GLintptr>(command.destinationOffset),
                                     static_cast<GLsizeiptr>(command.size));
        destination->writtenFrame = m_frameIndex;
    }

    void OpenGLDevice::BarrierBefore(const PassDesc& pass)
    {
        if (m_storageWritten.empty())
        {
            return;
        }
        GLbitfield wanted = 0;
        for (const PassResourceUse& use : pass.uses)
        {
            const GlBuffer* buffer = BufferOf(use);
            if (buffer == nullptr)
            {
                continue;
            }
            const auto written = m_storageWritten.find(buffer->name);
            if (written == m_storageWritten.end())
            {
                continue;
            }
            wanted |= BarrierBitsOf(use.usage) & ~written->second;
        }
        MakeVisible(wanted);
    }

    OpenGLDevice::GlBuffer* OpenGLDevice::BufferOf(const PassResourceUse& use)
    {
        return use.texture.IsEmpty() ? m_buffers.Find(use.buffer) : nullptr;
    }

    void OpenGLDevice::MakeVisible(std::uint32_t bits)
    {
        if (bits == 0)
        {
            return;
        }
        m_gl->MemoryBarrier(bits);
        for (auto& [name, issued] : m_storageWritten)
        {
            issued |= bits;
        }
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

    void OpenGLDevice::SampleLevels(GlTexture& texture, std::optional<std::uint32_t> level)
    {
        if (texture.sampledLevel == level)
        {
            return;
        }
        const std::uint32_t last = texture.desc.mipLevels - 1;
        const std::uint32_t base = level ? std::min(*level, last) : 0;
        m_gl->TextureParameteri(texture.name, GL_TEXTURE_BASE_LEVEL, static_cast<GLint>(base));
        m_gl->TextureParameteri(texture.name, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(level ? base : last));
        texture.sampledLevel = level;
    }

    std::uint32_t OpenGLDevice::FramebufferFor(const PassDesc& pass)
    {
        std::vector<FramebufferAttachment> names;
        for (const ColourAttachment& colour : pass.colour)
        {
            names.push_back({m_textures.Find(colour.texture)->name, colour.mipLevel, colour.layer});
        }
        names.push_back({pass.depth ? m_textures.Find(pass.depth->texture)->name : 0, 0,
                         pass.depth && pass.depth->layered ? LayeredAttachment : (pass.depth ? pass.depth->layer : 0)});
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
            const FramebufferAttachment& colour = names[i];
            const auto level = static_cast<GLint>(colour.mipLevel);
            // A cube or cube array texture's face is a layer of it; a 2D texture is attached whole.
            if (m_textures.Find(pass.colour[i].texture)->desc.kind != TextureKind::Texture2D)
            {
                gl.NamedFramebufferTextureLayer(framebuffer, attachment, colour.name, level,
                                                static_cast<GLint>(colour.layer));
            }
            else
            {
                gl.NamedFramebufferTexture(framebuffer, attachment, colour.name, level);
            }
            drawBuffers.push_back(attachment);
        }
        if (const FramebufferAttachment& depth = names.back(); depth.name != 0)
        {
            // A cube or cube array's single layer, or every layer of it (layered: each primitive's gl_Layer picks).
            const bool hasLayers = m_textures.Find(pass.depth->texture)->desc.kind != TextureKind::Texture2D;
            if (hasLayers && depth.layer != LayeredAttachment)
            {
                gl.NamedFramebufferTextureLayer(framebuffer, GL_DEPTH_ATTACHMENT, depth.name, 0,
                                                static_cast<GLint>(depth.layer));
            }
            else
            {
                gl.NamedFramebufferTexture(framebuffer, GL_DEPTH_ATTACHMENT, depth.name, 0);
            }
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
            if (std::ranges::find(it->first, name, &FramebufferAttachment::name) != it->first.end())
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
        if (pipeline.vertexArray != 0)
        {
            m_gl->DeleteVertexArrays(1, &pipeline.vertexArray);
        }
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

    OpenGLDevice::Texels OpenGLDevice::ReadTexels(TextureHandle texture, std::uint32_t mipLevel)
    {
        const GlTexture* found = m_textures.Find(texture);
        if (found == nullptr || found->name == 0 || found->desc.samples > 1 ||
            found->desc.kind != TextureKind::Texture2D || found->desc.format == TextureFormat::Depth32Float ||
            mipLevel >= found->desc.mipLevels)
        {
            return {};
        }
        Texels texels;
        texels.width = std::max(found->desc.width >> mipLevel, 1u);
        texels.height = std::max(found->desc.height >> mipLevel, 1u);
        texels.rgba.resize(std::size_t{texels.width} * texels.height * 4);
        // GL returns memory row 0 first, which an upper-left clip origin made the top row.
        m_gl->GetTextureImage(found->name, static_cast<GLint>(mipLevel), GL_RGBA, GL_FLOAT,
                              static_cast<GLsizei>(texels.rgba.size() * sizeof(float)), texels.rgba.data());
        return texels;
    }

    std::uint32_t OpenGLDevice::GlName(BufferHandle buffer)
    {
        const GlBuffer* found = m_buffers.Find(buffer);
        return found != nullptr ? found->name : 0;
    }

    std::vector<std::byte> OpenGLDevice::ReadBufferNow(BufferHandle buffer)
    {
        const GlBuffer* found = m_buffers.Find(buffer);
        if (found == nullptr || found->name == 0)
        {
            return {};
        }
        // Reading back waits for every command that writes the buffer, a compute pass's included.
        std::vector<std::byte> bytes(found->size);
        m_gl->MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        m_gl->GetNamedBufferSubData(found->name, 0, static_cast<GLsizeiptr>(bytes.size()), bytes.data());
        return bytes;
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
