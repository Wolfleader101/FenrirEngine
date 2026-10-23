#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "FenrirAssets/ShaderContainer.hpp"

#include "Device/Handles.hpp"

namespace Fenrir::Graphics
{
    /** @brief What a buffer may be used as, as flags. The explicit APIs need them up front; GL ignores them. */
    enum class BufferUsage : std::uint32_t
    {
        None = 0,
        Vertex = 1u << 0,
        Index = 1u << 1,
        Uniform = 1u << 2,
        Storage = 1u << 3,
        CopySource = 1u << 4,
        CopyDestination = 1u << 5,
    };

    /** @brief What a texture may be used as, as flags. */
    enum class TextureUsage : std::uint32_t
    {
        None = 0,
        Sampled = 1u << 0,
        ColourAttachment = 1u << 1,
        DepthAttachment = 1u << 2,
        Storage = 1u << 3,
        CopySource = 1u << 4,
        CopyDestination = 1u << 5,
    };

    /** @brief Both usages' flags combined. */
    [[nodiscard]] constexpr BufferUsage operator|(BufferUsage a, BufferUsage b) noexcept
    {
        return static_cast<BufferUsage>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
    }

    /** @brief Both usages' flags combined. */
    [[nodiscard]] constexpr TextureUsage operator|(TextureUsage a, TextureUsage b) noexcept
    {
        return static_cast<TextureUsage>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
    }

    /** @brief Whether `usage` has every flag of `wanted`. */
    [[nodiscard]] constexpr bool HasUsage(BufferUsage usage, BufferUsage wanted) noexcept
    {
        return (static_cast<std::uint32_t>(usage) & static_cast<std::uint32_t>(wanted)) ==
               static_cast<std::uint32_t>(wanted);
    }

    /** @brief Whether `usage` has every flag of `wanted`. */
    [[nodiscard]] constexpr bool HasUsage(TextureUsage usage, TextureUsage wanted) noexcept
    {
        return (static_cast<std::uint32_t>(usage) & static_cast<std::uint32_t>(wanted)) ==
               static_cast<std::uint32_t>(wanted);
    }

    /** @brief A buffer's size and uses. */
    struct BufferDesc
    {
        std::size_t size = 0;
        BufferUsage usage = BufferUsage::None;

        friend bool operator==(const BufferDesc&, const BufferDesc&) = default;
    };

    /** @brief A texel format. Colour formats hold linear values unless named sRGB. */
    enum class TextureFormat : std::uint8_t
    {
        Rgba8Unorm,
        Rgba8Srgb,
        Rgba16Float,
        Depth32Float,
        Rgba32Float,
        /** @brief One linear channel, read by shaders as red; the debug font's signed-distance-field atlas. */
        R8Unorm,
    };

    /** @brief Whether a texture is one image or six cube faces. */
    enum class TextureKind : std::uint8_t
    {
        Texture2D,
        Cube,
    };

    /** @brief A texture's shape, format and uses. Rows are stored top row first on every backend (ADR-0016). */
    struct TextureDesc
    {
        TextureKind kind = TextureKind::Texture2D;
        TextureFormat format = TextureFormat::Rgba8Unorm;
        std::uint32_t width = 1;
        std::uint32_t height = 1;
        std::uint32_t mipLevels = 1;
        /** @brief Samples per texel: 1 unless it is a multisampled attachment. */
        std::uint32_t samples = 1;
        TextureUsage usage = TextureUsage::Sampled;

        friend bool operator==(const TextureDesc&, const TextureDesc&) = default;
    };

    /** @brief How a sampler filters between texels or mips. */
    enum class Filter : std::uint8_t
    {
        Nearest,
        Linear,
    };

    /** @brief What a sampler reads outside [0, 1]. */
    enum class AddressMode : std::uint8_t
    {
        Repeat,
        MirroredRepeat,
        ClampToEdge,
    };

    /** @brief How textures bound with a sampler are filtered and addressed. */
    struct SamplerDesc
    {
        Filter minFilter = Filter::Linear;
        Filter magFilter = Filter::Linear;
        Filter mipFilter = Filter::Linear;
        AddressMode addressU = AddressMode::Repeat;
        AddressMode addressV = AddressMode::Repeat;
        AddressMode addressW = AddressMode::Repeat;
        /** @brief The maximum anisotropy; 1 turns it off. */
        float maxAnisotropy = 1.0f;

        friend bool operator==(const SamplerDesc&, const SamplerDesc&) = default;
    };

    /** @brief One binding a binding-group layout declares, at its binding number in the set. */
    struct BindingLayoutEntry
    {
        std::uint32_t binding = 0;
        /** @brief The shader container's resource kinds, so reflection and layouts compare directly. */
        ShaderBindingKind kind = ShaderBindingKind::UniformBuffer;

        friend bool operator==(const BindingLayoutEntry&, const BindingLayoutEntry&) = default;
    };

    /** @brief The bindings of one binding set, as pipelines and binding groups agree on them. */
    struct BindingGroupLayoutDesc
    {
        std::vector<BindingLayoutEntry> entries;
    };

    /**
     * @brief What one binding of a binding group holds: a range of a buffer for a block, or a texture with its sampler
     * for a texture binding.
     */
    struct BindingGroupEntry
    {
        std::uint32_t binding = 0;
        BufferHandle buffer;
        std::size_t offset = 0;
        /** @brief The range's size in bytes; 0 means to the end of the buffer. */
        std::size_t size = 0;
        TextureHandle texture;
        SamplerHandle sampler;

        friend bool operator==(const BindingGroupEntry&, const BindingGroupEntry&) = default;
    };

    /** @brief The resources bound to one binding set, laid out as its layout says. */
    struct BindingGroupDesc
    {
        BindingGroupLayoutHandle layout;
        std::vector<BindingGroupEntry> entries;
    };

    /** @brief How many binding sets a pipeline has: frame, view, material and draw (ADR-0024). */
    inline constexpr std::size_t BindingSetCount = 4;

    /** @brief The type of one vertex attribute in a vertex buffer. */
    enum class VertexFormat : std::uint8_t
    {
        Float,
        Float2,
        Float3,
        Float4,
        /** @brief Four bytes read as [0, 1] floats, such as a packed colour. */
        Unorm8x4,
    };

    /** @brief One vertex attribute: the shader location it feeds and where it sits in its buffer's vertex. */
    struct VertexAttribute
    {
        std::uint32_t location = 0;
        VertexFormat format = VertexFormat::Float3;
        std::uint32_t offset = 0;

        friend bool operator==(const VertexAttribute&, const VertexAttribute&) = default;
    };

    /** @brief One vertex buffer slot: its stride and the attributes read from it. */
    struct VertexBufferLayout
    {
        std::uint32_t stride = 0;
        std::vector<VertexAttribute> attributes;

        friend bool operator==(const VertexBufferLayout&, const VertexBufferLayout&) = default;
    };

    /** @brief How vertices are assembled into primitives. */
    enum class Topology : std::uint8_t
    {
        Triangles,
        Lines,
    };

    /** @brief Which faces are culled. Front faces are counter-clockwise as the viewer sees them on every backend. */
    enum class CullMode : std::uint8_t
    {
        None,
        Back,
    };

    /** @brief How a fragment's depth is compared with the stored one. Reversed-Z draws with `GreaterEqual`. */
    enum class CompareOp : std::uint8_t
    {
        Always,
        Less,
        LessEqual,
        Greater,
        GreaterEqual,
    };

    /** @brief How a fragment's colour combines with the target's. */
    enum class BlendMode : std::uint8_t
    {
        None,
        /** @brief Straight alpha: source times its alpha, plus destination times one minus it. */
        Alpha,
        /** @brief Premultiplied alpha: source, plus destination times one minus the source's alpha. */
        Premultiplied,
        /** @brief The source colour added to the destination's; the destination's alpha kept. */
        Additive,
    };

    /** @brief A pipeline's depth test and write. */
    struct DepthState
    {
        bool test = true;
        bool write = true;
        CompareOp compare = CompareOp::GreaterEqual;

        friend bool operator==(const DepthState&, const DepthState&) = default;
    };

    /**
     * @brief Everything one draw's state is, made once as the explicit APIs require: the shader variant to run, the
     * layouts of its four binding sets, its vertex input, rasterisation, depth, blending and the formats it draws into.
     */
    struct PipelineDesc
    {
        /**
         * @brief The shader variant, from a shader container: each backend picks the code it runs from it (the GL copy
         * under OpenGL). It must outlive the call only.
         */
        const ShaderVariant* shader = nullptr;
        /** @brief Each binding set's layout; an empty handle for a set the shader does not use. */
        std::array<BindingGroupLayoutHandle, BindingSetCount> sets{};
        std::vector<VertexBufferLayout> vertexBuffers;
        Topology topology = Topology::Triangles;
        CullMode cull = CullMode::Back;
        DepthState depth;
        BlendMode blend = BlendMode::None;
        std::vector<TextureFormat> colourFormats;
        /** @brief The depth attachment's format; empty when the pipeline draws with none. */
        std::optional<TextureFormat> depthFormat = TextureFormat::Depth32Float;
        std::uint32_t samples = 1;
    };
} // namespace Fenrir::Graphics
