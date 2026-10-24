#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Device/GraphicsDevice.hpp"
#include "Device/GraphicsSurface.hpp"
#include "Device/ResourceTable.hpp"

// glad's context of loaded GL functions, named only: its header stays inside src/Backend/OpenGL/ (ADR-0016).
struct GladGLContext;

namespace Fenrir
{
    class ILogger;
    struct GlProbe;
} // namespace Fenrir

namespace Fenrir::Graphics
{
    /** @brief A new OpenGL device, or why the backend could not start on the surface. */
    struct OpenGLDeviceResult
    {
        /** @brief Empty when the backend could not start. */
        std::unique_ptr<GraphicsDevice> device;
        /** @brief Why not; empty when it started. */
        std::string diagnostic;
    };

    /**
     * @brief Starts the OpenGL backend on `surface`, whose context must be OpenGL 4.6 core: makes it current, loads the
     * GL functions through it and sets the device's conventions ([0, 1] depth, sRGB writes to sRGB targets). With
     * `config.debugValidation`, GL's debug messages go to `logger` from a synchronous callback, as errors when they are
     * GL errors or of high severity; ask the surface for a debug context to get them all.
     *
     * Fails with a diagnostic, and never falls back to another backend, when the context cannot be made current, its
     * functions cannot be loaded, or it is not 4.6 core. `surface` and `logger` must outlive the device.
     */
    [[nodiscard]] OpenGLDeviceResult CreateOpenGLDevice(GraphicsSurface& surface, const DeviceConfig& config,
                                                        ILogger& logger);

    /**
     * @brief The OpenGL 4.6 backend (ADR-0016): emulates the graphics device's explicit shape with direct state access,
     * immutable storage, sampler objects, clip control and SPIR-V shaders, the GL copy of each shader variant
     * specialised and its bindings bound by GlBindingOf. One linked program serves every pipeline with the same vertex
     * and fragment code. GL synchronises itself, so it derives no barriers, and a destroyed object's GL name is deleted
     * when its slot is freed, `framesInFlight` frames later, as on every backend.
     *
     * Each pass sets the clip origin (spike lesson 3): upper-left for offscreen targets, so they are stored top row
     * first like uploaded images, and lower-left for passes drawing to the backbuffer, whose row 0 is the window's
     * bottom. Front faces are counter-clockwise in both, and pixel rectangles count from the target's top-left corner.
     *
     * Every call must come from the thread the context is current on, the main thread (ADR-0020).
     */
    class OpenGLDevice final : public GraphicsDevice
    {
      public:
        ~OpenGLDevice() override;

        OpenGLDevice(const OpenGLDevice&) = delete;
        OpenGLDevice& operator=(const OpenGLDevice&) = delete;
        OpenGLDevice(OpenGLDevice&&) = delete;
        OpenGLDevice& operator=(OpenGLDevice&&) = delete;

        [[nodiscard]] std::uint32_t FramesInFlight() const override;
        void BeginFrame(std::uint64_t frameIndex) override;

        [[nodiscard]] BufferHandle CreateBuffer(const BufferDesc& desc, std::span<const std::byte> initial,
                                                std::string_view label) override;
        [[nodiscard]] TextureHandle CreateTexture(const TextureDesc& desc, std::string_view label) override;
        [[nodiscard]] SamplerHandle CreateSampler(const SamplerDesc& desc, std::string_view label) override;
        [[nodiscard]] BindingGroupLayoutHandle CreateBindingGroupLayout(const BindingGroupLayoutDesc& desc,
                                                                        std::string_view label) override;
        [[nodiscard]] BindingGroupHandle CreateBindingGroup(const BindingGroupDesc& desc,
                                                            std::string_view label) override;
        [[nodiscard]] PipelineResult CreatePipeline(const PipelineDesc& desc, std::string_view label) override;

        bool WriteBuffer(BufferHandle buffer, std::size_t offset, std::span<const std::byte> bytes) override;
        /** @copydoc GraphicsDevice::WriteTexture Texels of the wrong size for that level are refused, logged. */
        bool WriteTexture(TextureHandle texture, std::uint32_t mipLevel, std::uint32_t layer,
                          std::span<const std::byte> texels) override;

        bool Destroy(BufferHandle buffer) override;
        bool Destroy(TextureHandle texture) override;
        bool Destroy(SamplerHandle sampler) override;
        bool Destroy(BindingGroupLayoutHandle layout) override;
        bool Destroy(BindingGroupHandle group) override;
        bool Destroy(PipelineHandle pipeline) override;

        [[nodiscard]] bool IsLive(BufferHandle buffer) const override;
        [[nodiscard]] bool IsLive(TextureHandle texture) const override;
        [[nodiscard]] bool IsLive(SamplerHandle sampler) const override;
        [[nodiscard]] bool IsLive(BindingGroupLayoutHandle layout) const override;
        [[nodiscard]] bool IsLive(BindingGroupHandle group) const override;
        [[nodiscard]] bool IsLive(PipelineHandle pipeline) const override;

        [[nodiscard]] TextureHandle Backbuffer() const override;
        void Submit(const CommandList& list) override;
        void Present() override;
        void WaitIdle() override;
        /** @copydoc GraphicsDevice::SetVsync Sets the surface's swap interval: 0 off, 1 on, -1 adaptive. */
        Vsync SetVsync(Vsync mode) override;

      private:
        friend OpenGLDeviceResult CreateOpenGLDevice(GraphicsSurface& surface, const DeviceConfig& config,
                                                     ILogger& logger);
        friend struct Fenrir::GlProbe;

        /** @brief A GL buffer: its name and size in bytes. */
        struct GlBuffer
        {
            std::uint32_t name = 0;
            std::size_t size = 0;
        };

        /** @brief A GL texture: its name (0 for the backbuffer, the default framebuffer) and what it was made as. */
        struct GlTexture
        {
            std::uint32_t name = 0;
            TextureDesc desc;
        };

        /** @brief A binding group with each entry's resource kind, read from its layout when it was made. */
        struct GlBindingGroup
        {
            std::vector<BindingGroupEntry> entries;
            std::vector<ShaderBindingKind> kinds;
            /** @brief Each entry's index among its layout's bindings of its kind, for GlBindingOf. */
            std::vector<std::uint32_t> indicesInKind;
        };

        /** @brief The code of one program: the GL SPIR-V of its vertex and fragment stages. */
        struct ProgramKey
        {
            std::vector<std::uint32_t> vertex;
            std::vector<std::uint32_t> fragment;

            friend auto operator<=>(const ProgramKey&, const ProgramKey&) = default;
        };

        /** @brief A linked program and how many live or pending pipelines use it. */
        struct GlProgram
        {
            std::uint32_t name = 0;
            std::uint32_t users = 0;
        };

        /** @brief A pipeline: its program's code, the vertex array of its vertex input, and its fixed state. */
        struct GlPipeline
        {
            const ProgramKey* program = nullptr;
            std::uint32_t programName = 0;
            std::uint32_t vertexArray = 0;
            std::vector<std::uint32_t> strides;
            Topology topology = Topology::Triangles;
            CullMode cull = CullMode::Back;
            DepthState depth;
            BlendMode blend = BlendMode::None;
            bool encodeSrgb = true;
        };

        /** @brief A buffer bound for vertices or indices: its GL name and the offset of its first byte. */
        struct BoundBuffer
        {
            std::uint32_t name = 0;
            std::size_t offset = 0;
        };

        /**
         * @brief What the commands replayed so far in the current pass left bound, for the ones that follow. Vertex and
         * index buffers outlast a pipeline change, as in the explicit APIs, so they are kept here and given to each
         * pipeline's vertex array as it is set.
         */
        struct ReplayState
        {
            /** @brief Whether the pass is skipped, every command up to its end ignored: its targets are stale. */
            bool skipping = false;
            bool drawingToBackbuffer = false;
            std::uint32_t targetHeight = 0;
            const GlPipeline* pipeline = nullptr;
            std::vector<BoundBuffer> vertexBuffers;
            BoundBuffer indexBuffer;
            IndexFormat indexFormat = IndexFormat::UInt32;
        };

        /** @brief A texture's texels as linear floats, four per texel, top row first; empty if it cannot be read. */
        struct Texels
        {
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            std::vector<float> rgba;
        };

        OpenGLDevice(GraphicsSurface& surface, const DeviceConfig& config, ILogger& logger);

        /** @brief Loads GL through the surface and checks it is 4.6 core; false with `diagnostic` if not. */
        bool Start(std::string& diagnostic);

        void Execute(const CmdBeginPass& command);
        void Execute(const CmdEndPass& command);
        void Execute(const CmdSetPipeline& command);
        void Execute(const CmdSetBindingGroup& command);
        void Execute(const CmdSetVertexBuffer& command);
        void Execute(const CmdSetIndexBuffer& command);
        void Execute(const CmdSetViewport& command);
        void Execute(const CmdSetScissor& command);
        void Execute(const CmdDraw& command);
        void Execute(const CmdDrawIndexed& command);
        void Execute(const CmdDispatch& command);
        void Execute(const CmdCopyBuffer& command);

        /** @brief The framebuffer drawing into `pass`'s attachments, made the first time they are drawn into together.
         */
        std::uint32_t FramebufferFor(const PassDesc& pass);
        /** @brief Deletes the framebuffers that draw into the texture named `name`, as it is deleted. */
        void ForgetFramebuffers(std::uint32_t name);
        /** @brief Lets go of a destroyed pipeline's GL objects, deleting its program once no pipeline uses it. */
        void ReleasePipeline(GlPipeline& pipeline);
        /** @brief `rect` from the target's top-left corner, in GL's window coordinates for the current pass. */
        [[nodiscard]] std::int32_t WindowY(const PixelRect& rect) const;

        /** @brief Gives the current pipeline's vertex array the vertex and index buffers bound in the pass. */
        void BindVertexInput();

        /** @brief For GlProbe: mip 0 of an offscreen colour texture as floats, top row first; empty if it cannot. */
        [[nodiscard]] Texels ReadTexels(TextureHandle texture);
        /** @brief For GlProbe: the GL name behind a live buffer, else 0. */
        [[nodiscard]] std::uint32_t GlName(BufferHandle buffer);
        /** @brief For GlProbe: the GL name behind a live texture, else 0. */
        [[nodiscard]] std::uint32_t GlName(TextureHandle texture);
        /** @brief For GlProbe: whether `name` is a GL buffer object, as GL sees it now. */
        [[nodiscard]] bool IsGlBuffer(std::uint32_t name) const;
        /** @brief For GlProbe: whether `name` is a GL texture object, as GL sees it now. */
        [[nodiscard]] bool IsGlTexture(std::uint32_t name) const;

        GraphicsSurface* m_surface;
        DeviceConfig m_config;
        ILogger* m_logger;
        std::unique_ptr<GladGLContext> m_gl;
        /** @brief Whether Start succeeded, so the GL functions are loaded and there are GL objects to delete. */
        bool m_started = false;
        std::uint64_t m_frameIndex = 0;

        ResourceTable<BufferHandle, GlBuffer> m_buffers;
        ResourceTable<TextureHandle, GlTexture> m_textures;
        ResourceTable<SamplerHandle, std::uint32_t> m_samplers;
        ResourceTable<BindingGroupLayoutHandle, BindingGroupLayoutDesc> m_layouts;
        ResourceTable<BindingGroupHandle, GlBindingGroup> m_groups;
        ResourceTable<PipelineHandle, GlPipeline> m_pipelines;
        TextureHandle m_backbuffer;

        std::map<ProgramKey, GlProgram> m_programs;
        /** @brief Framebuffers by the GL names of their colour attachments then depth (0 for none). */
        std::map<std::vector<std::uint32_t>, std::uint32_t> m_framebuffers;
        ReplayState m_replay;
    };
} // namespace Fenrir::Graphics
