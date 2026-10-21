#pragma once

// SPIKE (prototype/fenrir-renderer-spike, throwaway): the private graphics device of ADR-0016, shaped like the explicit
// APIs: buffers, textures, samplers, shaders from SPIR-V, binding-group layouts and groups, pipelines, and passes
// recorded into a plain-data CommandList that Submit replays. No GL type appears here.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace Fenrir::Gpu
{
    template <typename Tag>
    struct Handle
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0; // 0: empty

        [[nodiscard]] bool IsValid() const
        {
            return generation != 0;
        }
        friend bool operator==(const Handle&, const Handle&) = default;
    };

    using BufferHandle = Handle<struct BufferTag>;
    using TextureHandle = Handle<struct TextureTag>;
    using SamplerHandle = Handle<struct SamplerTag>;
    using ShaderHandle = Handle<struct ShaderTag>;
    using BindGroupLayoutHandle = Handle<struct BindGroupLayoutTag>;
    using BindGroupHandle = Handle<struct BindGroupTag>;
    using PipelineHandle = Handle<struct PipelineTag>;

    enum class BufferUsage : std::uint32_t
    {
        Vertex = 1,
        Index = 2,
        Uniform = 4,
        Storage = 8,
    };

    struct BufferDesc
    {
        std::size_t size = 0;
        std::uint32_t usage = 0; // BufferUsage bits
        const char* label = "";
    };

    enum class TextureFormat
    {
        None,
        Rgba8Unorm,
        Rgba8Srgb,
        Rgba16Float,
        Depth32Float,
    };

    struct TextureDesc
    {
        TextureFormat format = TextureFormat::Rgba8Unorm;
        int width = 1;
        int height = 1;
        int mipLevels = 1;
        bool cube = false;
        bool renderTarget = false;
        const char* label = "";
    };

    struct SamplerDesc
    {
        bool linear = true;
        bool mipmaps = true;
        bool repeat = true;
        float anisotropy = 8.0f;
        friend bool operator==(const SamplerDesc&, const SamplerDesc&) = default;
    };

    enum class ShaderStage
    {
        Vertex,
        Fragment,
    };

    struct ShaderDesc
    {
        ShaderStage stage = ShaderStage::Vertex;
        std::span<const std::uint32_t> spirv; // the backend's flattened SPIR-V from the shader container
    };

    enum class BindingKind : std::uint32_t
    {
        UniformBuffer = 0,
        StorageBuffer = 1,
        Texture = 2,
    };

    struct BindGroupLayoutEntry
    {
        std::uint32_t binding = 0;
        BindingKind kind = BindingKind::UniformBuffer;
    };

    struct BindGroupLayoutDesc
    {
        std::vector<BindGroupLayoutEntry> entries;
    };

    struct BindGroupEntry
    {
        std::uint32_t binding = 0;
        BufferHandle buffer;
        std::size_t offset = 0;
        std::size_t size = 0;
        TextureHandle texture;
        SamplerHandle sampler;
    };

    struct BindGroupDesc
    {
        BindGroupLayoutHandle layout;
        std::vector<BindGroupEntry> entries;
    };

    enum class VertexFormat
    {
        Float2,
        Float3,
        Float4,
    };

    struct VertexAttribute
    {
        std::uint32_t location = 0;
        VertexFormat format = VertexFormat::Float3;
        std::uint32_t offset = 0;
    };

    struct VertexLayout
    {
        std::uint32_t stride = 0;
        std::vector<VertexAttribute> attributes;
    };

    enum class Topology
    {
        Triangles,
        Lines,
    };

    enum class CompareOp
    {
        Always,
        GreaterEqual, // reversed-Z (ADR-0026)
    };

    enum class Blend
    {
        None,
        Alpha,
    };

    /** @brief Everything a draw's state is, made once, as the explicit APIs require. */
    struct PipelineDesc
    {
        ShaderHandle vertex;
        ShaderHandle fragment;
        std::array<BindGroupLayoutHandle, 4> sets{};
        VertexLayout vertexLayout;
        Topology topology = Topology::Triangles;
        bool cullBack = true; // front faces are counter-clockwise as the viewer sees them, on every backend
        bool depthTest = true;
        bool depthWrite = true;
        CompareOp depthCompare = CompareOp::GreaterEqual;
        Blend blend = Blend::None;
        TextureFormat colourFormat = TextureFormat::Rgba16Float;
        TextureFormat depthFormat = TextureFormat::Depth32Float;
        const char* label = "";
    };

    enum class LoadOp
    {
        Load,
        Clear,
        DontCare,
    };

    /** @brief How a pass uses a resource, for the barriers an explicit backend derives (GL ignores it). */
    enum class Usage
    {
        ColourAttachment,
        DepthAttachment,
        DepthReadOnly,
        Sampled,
    };

    struct PassDesc
    {
        TextureHandle colour; // empty: the backbuffer
        LoadOp colourLoad = LoadOp::Load;
        std::array<float, 4> clearColour{0, 0, 0, 1};
        TextureHandle depth;
        LoadOp depthLoad = LoadOp::Load;
        float clearDepth = 0.0f; // reversed-Z clears to 0
        bool depthReadOnly = false;
        std::vector<std::pair<TextureHandle, Usage>> sampled; // declared for barriers
        int width = 0;
        int height = 0;
        const char* label = "";
    };

    // The commands, plain structs.
    struct CmdBeginPass
    {
        PassDesc desc;
    };
    struct CmdEndPass
    {
    };
    struct CmdSetPipeline
    {
        PipelineHandle pipeline;
    };
    struct CmdSetBindGroup
    {
        std::uint32_t set = 0;
        BindGroupHandle group;
    };
    struct CmdSetVertexBuffer
    {
        BufferHandle buffer;
        std::size_t offset = 0;
    };
    struct CmdSetIndexBuffer
    {
        BufferHandle buffer;
    };
    struct CmdSetViewport
    {
        int x = 0, y = 0, width = 0, height = 0; // pixels, from the top-left (ADR-0016)
    };
    struct CmdDraw
    {
        std::uint32_t vertexCount = 0;
        std::uint32_t firstVertex = 0;
        std::uint32_t firstInstance = 0;
    };
    struct CmdDrawIndexed
    {
        std::uint32_t indexCount = 0;
        std::uint32_t firstIndex = 0;
        std::int32_t baseVertex = 0;
        std::uint32_t firstInstance = 0;
    };

    using Command = std::variant<CmdBeginPass, CmdEndPass, CmdSetPipeline, CmdSetBindGroup, CmdSetVertexBuffer,
                                 CmdSetIndexBuffer, CmdSetViewport, CmdDraw, CmdDrawIndexed>;

    struct CommandList
    {
        std::vector<Command> commands;
    };

    /** @brief Records one pass into its own CommandList. Needs no backend. */
    class PassEncoder
    {
      public:
        PassEncoder(CommandList& list, const PassDesc& desc) : m_list(list)
        {
            m_list.commands.emplace_back(CmdBeginPass{desc});
        }
        ~PassEncoder()
        {
            m_list.commands.emplace_back(CmdEndPass{});
        }
        PassEncoder(const PassEncoder&) = delete;
        PassEncoder& operator=(const PassEncoder&) = delete;

        void SetPipeline(PipelineHandle p)
        {
            m_list.commands.emplace_back(CmdSetPipeline{p});
        }
        void SetBindGroup(std::uint32_t set, BindGroupHandle g)
        {
            m_list.commands.emplace_back(CmdSetBindGroup{set, g});
        }
        void SetVertexBuffer(BufferHandle b, std::size_t offset = 0)
        {
            m_list.commands.emplace_back(CmdSetVertexBuffer{b, offset});
        }
        void SetIndexBuffer(BufferHandle b)
        {
            m_list.commands.emplace_back(CmdSetIndexBuffer{b});
        }
        void SetViewport(int x, int y, int w, int h)
        {
            m_list.commands.emplace_back(CmdSetViewport{x, y, w, h});
        }
        void Draw(std::uint32_t vertexCount, std::uint32_t firstVertex = 0, std::uint32_t firstInstance = 0)
        {
            m_list.commands.emplace_back(CmdDraw{vertexCount, firstVertex, firstInstance});
        }
        void DrawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex, std::int32_t baseVertex,
                         std::uint32_t firstInstance)
        {
            m_list.commands.emplace_back(CmdDrawIndexed{indexCount, firstIndex, baseVertex, firstInstance});
        }

      private:
        CommandList& m_list;
    };

    /** @brief The core's abstract surface (ADR-0022): the facade adapts FenrirWindow to it. */
    class GraphicsSurface
    {
      public:
        virtual ~GraphicsSurface() = default;
        [[nodiscard]] virtual void* GetProcAddress(const char* name) = 0;
        virtual bool MakeCurrent() = 0;
        virtual void Present() = 0;
        virtual bool SetSwapInterval(int interval) = 0;
        virtual void GetPixelSize(int& width, int& height) = 0;
    };

    enum class Backend
    {
        OpenGL,
        Null,
    };

    struct DeviceStats
    {
        std::uint32_t draws = 0;
        std::uint32_t passes = 0;
        std::uint32_t pipelines = 0;
        std::uint32_t liveBuffers = 0;
        std::uint32_t liveTextures = 0;
    };

    class GraphicsDevice
    {
      public:
        virtual ~GraphicsDevice() = default;

        virtual BufferHandle CreateBuffer(const BufferDesc& desc, std::span<const std::byte> initial = {}) = 0;
        virtual void WriteBuffer(BufferHandle buffer, std::size_t offset, std::span<const std::byte> bytes) = 0;
        virtual TextureHandle CreateTexture(const TextureDesc& desc) = 0;
        /** @brief Uploads mip 0 of one face (0 for 2D) and, if the texture has mips, regenerates them. */
        virtual void WriteTexture(TextureHandle texture, int face, std::span<const std::byte> pixels) = 0;
        virtual SamplerHandle CreateSampler(const SamplerDesc& desc) = 0;
        /** @brief Null and a message if the backend rejects the SPIR-V. */
        virtual ShaderHandle CreateShader(const ShaderDesc& desc, std::string& error) = 0;
        virtual BindGroupLayoutHandle CreateBindGroupLayout(const BindGroupLayoutDesc& desc) = 0;
        virtual BindGroupHandle CreateBindGroup(const BindGroupDesc& desc) = 0;
        virtual PipelineHandle CreatePipeline(const PipelineDesc& desc) = 0;

        // Destruction is deferred until no frame in flight can use the object (ADR-0016).
        virtual void Destroy(BufferHandle) = 0;
        virtual void Destroy(TextureHandle) = 0;
        virtual void Destroy(BindGroupHandle) = 0;

        virtual void BeginFrame(std::uint64_t frameIndex) = 0;
        virtual void Submit(const CommandList& list) = 0;
        virtual void Present() = 0;
        virtual bool SetSwapInterval(int interval) = 0;
        virtual void GetBackbufferSize(int& width, int& height) = 0;
        /** @brief Reads the backbuffer as RGBA8, top row first. For tests and the spike's screenshot. */
        virtual std::vector<std::uint8_t> ReadBackbuffer(int& width, int& height) = 0;

        [[nodiscard]] virtual DeviceStats Stats() const = 0;
    };

    std::unique_ptr<GraphicsDevice> CreateNullDevice();
    /** @brief Null and a message if no 4.6 core context with the needed features is current on the surface. */
    std::unique_ptr<GraphicsDevice> CreateOpenGLDevice(GraphicsSurface& surface, bool debug, std::string& error);

    /** @brief Shared with FenrirShaderCompiler: GL binding = set * BindingsPerSet + binding, per kind. */
    inline constexpr std::uint32_t BindingsPerSet = 4;
} // namespace Fenrir::Gpu
