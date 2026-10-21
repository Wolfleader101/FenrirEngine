// SPIKE (prototype/fenrir-renderer-spike, throwaway): the OpenGL 4.6 backend. The only file that includes GL. Emulates
// the explicit shape with DSA, immutable storage, samplers, clip control and SPIR-V (ADR-0016, ADR-0018).

#include "Device/Device.hpp"
#include "Device/HandlePool.hpp"

#include <glad/glad.h>

#include <cstdio>
#include <map>
#include <utility>

namespace Fenrir::Gpu
{
    namespace
    {
        struct GLBuffer
        {
            GLuint id = 0;
            std::size_t size = 0;
        };
        struct GLTexture
        {
            GLuint id = 0;
            TextureDesc desc;
        };
        struct GLShader
        {
            GLuint id = 0;
            ShaderStage stage = ShaderStage::Vertex;
        };
        struct GLLayout
        {
            BindGroupLayoutDesc desc;
        };
        struct GLGroup
        {
            BindGroupDesc desc;
            std::vector<BindingKind> kinds; // per entry, from the layout
        };
        struct GLPipeline
        {
            GLuint program = 0;
            GLuint vao = 0;
            PipelineDesc desc;
        };

        GLenum InternalFormat(TextureFormat f)
        {
            switch (f)
            {
            case TextureFormat::Rgba8Unorm:
                return GL_RGBA8;
            case TextureFormat::Rgba8Srgb:
                return GL_SRGB8_ALPHA8;
            case TextureFormat::Rgba16Float:
                return GL_RGBA16F;
            case TextureFormat::Depth32Float:
                return GL_DEPTH_COMPONENT32F;
            default:
                return GL_RGBA8;
            }
        }

        void APIENTRY DebugCallback(GLenum, GLenum type, GLuint, GLenum severity, GLsizei, const GLchar* message,
                                    const void* user)
        {
            if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
            {
                return;
            }
            if (type == GL_DEBUG_TYPE_ERROR)
            {
                ++*static_cast<std::uint32_t*>(const_cast<void*>(user));
            }
            std::fprintf(stderr, "[GL] %s\n", message);
        }

        class GLDevice final : public GraphicsDevice
        {
          public:
            explicit GLDevice(GraphicsSurface& surface) : m_surface(surface)
            {
            }

            bool Init(bool debug, std::string& error)
            {
                if (gladLoadGLLoader([](const char* name) { return s_surface->GetProcAddress(name); }) == 0)
                {
                    error = "could not load OpenGL";
                    return false;
                }
                if (GLVersion.major < 4 || (GLVersion.major == 4 && GLVersion.minor < 6))
                {
                    error = "OpenGL 4.6 core is required, the context is " + std::to_string(GLVersion.major) + "." +
                            std::to_string(GLVersion.minor);
                    return false;
                }
                if (debug)
                {
                    glEnable(GL_DEBUG_OUTPUT);
                    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
                    glDebugMessageCallback(DebugCallback, &m_glErrors);
                }
                // The device's conventions (ADR-0016, ADR-0026): [0, 1] depth, upper-left origin. The origin is set
                // per pass (BeginPass): upper-left for textures, lower-left for the window.
                glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE);
                glEnable(GL_FRAMEBUFFER_SRGB); // the backbuffer is sRGB; float targets are unaffected
                glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
                std::fprintf(stderr, "[GL] %s, %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
                             reinterpret_cast<const char*>(glGetString(GL_VERSION)));
                return true;
            }

            static inline GraphicsSurface* s_surface = nullptr;

            BufferHandle CreateBuffer(const BufferDesc& desc, std::span<const std::byte> initial) override
            {
                GLBuffer b;
                b.size = desc.size;
                glCreateBuffers(1, &b.id);
                glNamedBufferStorage(b.id, static_cast<GLsizeiptr>(desc.size),
                                     initial.empty() ? nullptr : initial.data(), GL_DYNAMIC_STORAGE_BIT);
                glObjectLabel(GL_BUFFER, b.id, -1, desc.label);
                return m_buffers.Add(b);
            }

            void WriteBuffer(BufferHandle h, std::size_t offset, std::span<const std::byte> bytes) override
            {
                if (GLBuffer* b = m_buffers.Get(h); b != nullptr && !bytes.empty())
                {
                    glNamedBufferSubData(b->id, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(bytes.size()),
                                         bytes.data());
                }
            }

            TextureHandle CreateTexture(const TextureDesc& desc) override
            {
                GLTexture t;
                t.desc = desc;
                glCreateTextures(desc.cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D, 1, &t.id);
                glTextureStorage2D(t.id, desc.mipLevels, InternalFormat(desc.format), desc.width, desc.height);
                glObjectLabel(GL_TEXTURE, t.id, -1, desc.label);
                return m_textures.Add(t);
            }

            void WriteTexture(TextureHandle h, int face, std::span<const std::byte> pixels) override
            {
                GLTexture* t = m_textures.Get(h);
                if (t == nullptr)
                {
                    return;
                }
                if (t->desc.cube)
                {
                    glTextureSubImage3D(t->id, 0, 0, 0, face, t->desc.width, t->desc.height, 1, GL_RGBA,
                                        GL_UNSIGNED_BYTE, pixels.data());
                }
                else
                {
                    glTextureSubImage2D(t->id, 0, 0, 0, t->desc.width, t->desc.height, GL_RGBA, GL_UNSIGNED_BYTE,
                                        pixels.data());
                }
                if (t->desc.mipLevels > 1)
                {
                    glGenerateTextureMipmap(t->id);
                }
            }

            SamplerHandle CreateSampler(const SamplerDesc& desc) override
            {
                GLuint id = 0;
                glCreateSamplers(1, &id);
                const GLint wrap = desc.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
                glSamplerParameteri(id, GL_TEXTURE_WRAP_S, wrap);
                glSamplerParameteri(id, GL_TEXTURE_WRAP_T, wrap);
                glSamplerParameteri(id, GL_TEXTURE_WRAP_R, wrap);
                glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, desc.linear ? GL_LINEAR : GL_NEAREST);
                glSamplerParameteri(id, GL_TEXTURE_MIN_FILTER,
                                    desc.mipmaps ? (desc.linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST)
                                                 : (desc.linear ? GL_LINEAR : GL_NEAREST));
                if (desc.anisotropy > 1.0f)
                {
                    glSamplerParameterf(id, GL_TEXTURE_MAX_ANISOTROPY, desc.anisotropy);
                }
                return m_samplers.Add(id);
            }

            ShaderHandle CreateShader(const ShaderDesc& desc, std::string& error) override
            {
                const GLenum type = desc.stage == ShaderStage::Vertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER;
                GLuint id = glCreateShader(type);
                glShaderBinary(1, &id, GL_SHADER_BINARY_FORMAT_SPIR_V, desc.spirv.data(),
                               static_cast<GLsizei>(desc.spirv.size_bytes()));
                glSpecializeShader(id, "main", 0, nullptr, nullptr);
                GLint ok = 0;
                glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
                if (ok == 0)
                {
                    char log[2048] = {};
                    glGetShaderInfoLog(id, sizeof(log), nullptr, log);
                    error = log;
                    glDeleteShader(id);
                    return {};
                }
                return m_shaders.Add({id, desc.stage});
            }

            BindGroupLayoutHandle CreateBindGroupLayout(const BindGroupLayoutDesc& desc) override
            {
                return m_layouts.Add({desc});
            }

            BindGroupHandle CreateBindGroup(const BindGroupDesc& desc) override
            {
                GLGroup group;
                group.desc = desc;
                const GLLayout* layout = m_layouts.Get(desc.layout);
                for (const BindGroupEntry& e : desc.entries)
                {
                    BindingKind kind = BindingKind::UniformBuffer;
                    if (layout != nullptr)
                    {
                        for (const BindGroupLayoutEntry& le : layout->desc.entries)
                        {
                            if (le.binding == e.binding)
                                kind = le.kind;
                        }
                    }
                    group.kinds.push_back(kind);
                }
                return m_groups.Add(std::move(group));
            }

            PipelineHandle CreatePipeline(const PipelineDesc& desc) override
            {
                const GLShader* vs = m_shaders.Get(desc.vertex);
                const GLShader* fs = m_shaders.Get(desc.fragment);
                if (vs == nullptr || fs == nullptr)
                {
                    return {};
                }
                GLPipeline p;
                p.desc = desc;
                auto key = std::make_pair(vs->id, fs->id);
                if (auto it = m_programs.find(key); it != m_programs.end())
                {
                    p.program = it->second;
                }
                else
                {
                    p.program = glCreateProgram();
                    glAttachShader(p.program, vs->id);
                    glAttachShader(p.program, fs->id);
                    glLinkProgram(p.program);
                    GLint ok = 0;
                    glGetProgramiv(p.program, GL_LINK_STATUS, &ok);
                    if (ok == 0)
                    {
                        char log[2048] = {};
                        glGetProgramInfoLog(p.program, sizeof(log), nullptr, log);
                        std::fprintf(stderr, "[GL] link %s: %s\n", desc.label, log);
                        return {};
                    }
                    glObjectLabel(GL_PROGRAM, p.program, -1, desc.label);
                    m_programs[key] = p.program;
                }
                glCreateVertexArrays(1, &p.vao);
                for (const VertexAttribute& a : desc.vertexLayout.attributes)
                {
                    const GLint size = a.format == VertexFormat::Float2 ? 2 : a.format == VertexFormat::Float3 ? 3 : 4;
                    glEnableVertexArrayAttrib(p.vao, a.location);
                    glVertexArrayAttribFormat(p.vao, a.location, size, GL_FLOAT, GL_FALSE, a.offset);
                    glVertexArrayAttribBinding(p.vao, a.location, 0);
                }
                return m_pipelines.Add(p);
            }

            void Destroy(BufferHandle h) override
            {
                m_freeBuffers.Queue(h, m_frame);
            }
            void Destroy(TextureHandle h) override
            {
                m_freeTextures.Queue(h, m_frame);
            }
            void Destroy(BindGroupHandle h) override
            {
                m_freeGroups.Queue(h, m_frame);
            }

            void BeginFrame(std::uint64_t frameIndex) override
            {
                m_frame = frameIndex;
                m_stats.draws = 0;
                m_stats.passes = 0;
                m_freeBuffers.Collect(m_frame, 2, [this](BufferHandle h) {
                    GLBuffer b;
                    if (m_buffers.Remove(h, &b))
                        glDeleteBuffers(1, &b.id);
                });
                m_freeTextures.Collect(m_frame, 2, [this](TextureHandle h) {
                    GLTexture t;
                    if (m_textures.Remove(h, &t))
                    {
                        ForgetFramebuffers(t.id);
                        glDeleteTextures(1, &t.id);
                    }
                });
                m_freeGroups.Collect(m_frame, 2, [this](BindGroupHandle h) { m_groups.Remove(h); });
            }

            void Submit(const CommandList& list) override
            {
                for (const Command& command : list.commands)
                {
                    std::visit([this](const auto& c) { Execute(c); }, command);
                }
            }

            void Present() override
            {
                m_surface.Present();
            }

            bool SetSwapInterval(int interval) override
            {
                return m_surface.SetSwapInterval(interval);
            }

            void GetBackbufferSize(int& w, int& h) override
            {
                m_surface.GetPixelSize(w, h);
            }

            std::vector<std::uint8_t> ReadBackbuffer(int& w, int& h) override
            {
                m_surface.GetPixelSize(w, h);
                std::vector<std::uint8_t> rows(static_cast<std::size_t>(w * h * 4));
                std::vector<std::uint8_t> out(rows.size());
                glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
                glReadBuffer(GL_FRONT); // after Present: the frame just shown
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rows.data());
                const std::size_t stride = static_cast<std::size_t>(w) * 4;
                for (int y = 0; y < h; ++y) // GL reads bottom row first
                {
                    std::copy_n(rows.data() + static_cast<std::size_t>(h - 1 - y) * stride, stride,
                                out.data() + static_cast<std::size_t>(y) * stride);
                }
                return out;
            }

            DeviceStats Stats() const override
            {
                DeviceStats s = m_stats;
                s.pipelines = m_pipelines.LiveCount();
                s.liveBuffers = m_buffers.LiveCount();
                s.liveTextures = m_textures.LiveCount();
                return s;
            }

            std::uint32_t m_glErrors = 0;

          private:
            void Execute(const CmdBeginPass& c)
            {
                ++m_stats.passes;
                const PassDesc& d = c.desc;
                m_targetHeight = d.height;
                // Upper-left origin puts NDC +y at row 0, so render targets are stored top row first, as uploaded
                // images and every explicit API store them. The window's row 0 is its bottom, so a pass drawing to it
                // goes back to lower-left. Each flips window-space winding, so the front face follows (SetPipeline).
                m_backbufferPass = !d.colour.IsValid();
                glClipControl(m_backbufferPass ? GL_LOWER_LEFT : GL_UPPER_LEFT, GL_ZERO_TO_ONE);
                GLuint fbo = 0;
                if (d.colour.IsValid())
                {
                    const GLTexture* colour = m_textures.Get(d.colour);
                    const GLTexture* depth = m_textures.Get(d.depth);
                    fbo = Framebuffer(colour ? colour->id : 0, depth ? depth->id : 0);
                }
                glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, d.label);
                glBindFramebuffer(GL_FRAMEBUFFER, fbo);
                glViewport(0, 0, d.width, d.height);
                glDisable(GL_SCISSOR_TEST);
                if (d.colourLoad == LoadOp::Clear)
                {
                    glClearNamedFramebufferfv(fbo, GL_COLOR, 0, d.clearColour.data());
                }
                if (d.depth.IsValid() && d.depthLoad == LoadOp::Clear)
                {
                    glDepthMask(GL_TRUE);
                    glClearNamedFramebufferfv(fbo, GL_DEPTH, 0, &d.clearDepth);
                }
            }

            void Execute(const CmdEndPass&)
            {
                glPopDebugGroup();
            }

            void Execute(const CmdSetPipeline& c)
            {
                GLPipeline* p = m_pipelines.Get(c.pipeline);
                m_pipeline = p;
                if (p == nullptr)
                {
                    return;
                }
                const PipelineDesc& d = p->desc;
                glUseProgram(p->program);
                glBindVertexArray(p->vao);
                if (d.cullBack)
                {
                    glEnable(GL_CULL_FACE);
                    glCullFace(GL_BACK);
                    glFrontFace(GL_CCW);
                }
                else
                {
                    glDisable(GL_CULL_FACE);
                }
                if (d.depthTest)
                {
                    glEnable(GL_DEPTH_TEST);
                    glDepthFunc(d.depthCompare == CompareOp::GreaterEqual ? GL_GEQUAL : GL_ALWAYS);
                }
                else
                {
                    glDisable(GL_DEPTH_TEST);
                }
                glDepthMask(d.depthWrite ? GL_TRUE : GL_FALSE);
                if (d.blend == Blend::Alpha)
                {
                    glEnable(GL_BLEND);
                    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                }
                else
                {
                    glDisable(GL_BLEND);
                }
            }

            void Execute(const CmdSetBindGroup& c)
            {
                const GLGroup* g = m_groups.Get(c.group);
                if (g == nullptr)
                {
                    return;
                }
                for (std::size_t i = 0; i < g->desc.entries.size(); ++i)
                {
                    const BindGroupEntry& e = g->desc.entries[i];
                    const GLuint unit = c.set * BindingsPerSet + e.binding; // as FenrirShaderCompiler flattened it
                    switch (g->kinds[i])
                    {
                    case BindingKind::UniformBuffer:
                    case BindingKind::StorageBuffer: {
                        const GLBuffer* b = m_buffers.Get(e.buffer);
                        const GLenum target =
                            g->kinds[i] == BindingKind::UniformBuffer ? GL_UNIFORM_BUFFER : GL_SHADER_STORAGE_BUFFER;
                        if (b != nullptr)
                        {
                            glBindBufferRange(target, unit, b->id, static_cast<GLintptr>(e.offset),
                                              static_cast<GLsizeiptr>(e.size ? e.size : b->size - e.offset));
                        }
                        break;
                    }
                    case BindingKind::Texture: {
                        const GLTexture* t = m_textures.Get(e.texture);
                        const GLuint* s = m_samplers.Get(e.sampler);
                        glBindTextureUnit(unit, t ? t->id : 0);
                        glBindSampler(unit, s ? *s : 0);
                        break;
                    }
                    }
                }
            }

            void Execute(const CmdSetVertexBuffer& c)
            {
                const GLBuffer* b = m_buffers.Get(c.buffer);
                if (m_pipeline != nullptr && b != nullptr)
                {
                    glVertexArrayVertexBuffer(m_pipeline->vao, 0, b->id, static_cast<GLintptr>(c.offset),
                                              static_cast<GLsizei>(m_pipeline->desc.vertexLayout.stride));
                }
            }

            void Execute(const CmdSetIndexBuffer& c)
            {
                const GLBuffer* b = m_buffers.Get(c.buffer);
                if (m_pipeline != nullptr && b != nullptr)
                {
                    glVertexArrayElementBuffer(m_pipeline->vao, b->id);
                }
            }

            void Execute(const CmdSetViewport& c)
            {
                // Top-left pixels in; a texture's rows already count from the top here, the window's from the bottom.
                glViewport(c.x, m_backbufferPass ? m_targetHeight - c.y - c.height : c.y, c.width, c.height);
            }

            void Execute(const CmdDraw& c)
            {
                if (m_pipeline == nullptr)
                    return;
                ++m_stats.draws;
                glDrawArraysInstancedBaseInstance(Mode(), static_cast<GLint>(c.firstVertex),
                                                  static_cast<GLsizei>(c.vertexCount), 1, c.firstInstance);
            }

            void Execute(const CmdDrawIndexed& c)
            {
                if (m_pipeline == nullptr)
                    return;
                ++m_stats.draws;
                glDrawElementsInstancedBaseVertexBaseInstance(
                    Mode(), static_cast<GLsizei>(c.indexCount), GL_UNSIGNED_INT,
                    reinterpret_cast<const void*>(static_cast<std::uintptr_t>(c.firstIndex) * 4), 1, c.baseVertex,
                    c.firstInstance);
            }

            GLenum Mode() const
            {
                return m_pipeline->desc.topology == Topology::Lines ? GL_LINES : GL_TRIANGLES;
            }

            GLuint Framebuffer(GLuint colour, GLuint depth)
            {
                auto key = std::make_pair(colour, depth);
                if (auto it = m_framebuffers.find(key); it != m_framebuffers.end())
                {
                    return it->second;
                }
                GLuint fbo = 0;
                glCreateFramebuffers(1, &fbo);
                if (colour != 0)
                    glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, colour, 0);
                if (depth != 0)
                    glNamedFramebufferTexture(fbo, GL_DEPTH_ATTACHMENT, depth, 0);
                if (glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                {
                    std::fprintf(stderr, "[GL] incomplete framebuffer\n");
                }
                m_framebuffers[key] = fbo;
                return fbo;
            }

            void ForgetFramebuffers(GLuint texture)
            {
                for (auto it = m_framebuffers.begin(); it != m_framebuffers.end();)
                {
                    if (it->first.first == texture || it->first.second == texture)
                    {
                        glDeleteFramebuffers(1, &it->second);
                        it = m_framebuffers.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }

            GraphicsSurface& m_surface;
            std::uint64_t m_frame = 0;
            int m_targetHeight = 0;
            bool m_backbufferPass = false;
            DeviceStats m_stats;
            GLPipeline* m_pipeline = nullptr;
            HandlePool<BufferHandle, GLBuffer> m_buffers;
            HandlePool<TextureHandle, GLTexture> m_textures;
            HandlePool<SamplerHandle, GLuint> m_samplers;
            HandlePool<ShaderHandle, GLShader> m_shaders;
            HandlePool<BindGroupLayoutHandle, GLLayout> m_layouts;
            HandlePool<BindGroupHandle, GLGroup> m_groups;
            HandlePool<PipelineHandle, GLPipeline> m_pipelines;
            DeferredFrees<BufferHandle> m_freeBuffers;
            DeferredFrees<TextureHandle> m_freeTextures;
            DeferredFrees<BindGroupHandle> m_freeGroups;
            std::map<std::pair<GLuint, GLuint>, GLuint> m_programs;
            std::map<std::pair<GLuint, GLuint>, GLuint> m_framebuffers;
        };
    } // namespace

    std::unique_ptr<GraphicsDevice> CreateOpenGLDevice(GraphicsSurface& surface, bool debug, std::string& error)
    {
        if (!surface.MakeCurrent())
        {
            error = "could not make the GL context current";
            return nullptr;
        }
        auto device = std::make_unique<GLDevice>(surface);
        GLDevice::s_surface = &surface; // glad 1's loader takes a plain function pointer: no user data
        if (!device->Init(debug, error))
        {
            return nullptr;
        }
        return device;
    }
} // namespace Fenrir::Gpu
