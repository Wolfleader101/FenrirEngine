#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirAssets/ImageData.hpp"
#include "FenrirAssets/MaterialData.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    /** @name The first implementation's limits (ADR-0027)
     * A command over one is skipped by the renderer and logged once. They are limits of this version, not the design.
     * @{
     */
    /** @brief The most texture slots a 2D command may use. */
    inline constexpr std::size_t MaxTextureSlots2D = 4;
    /** @brief The most parameter bytes a 2D command may carry. */
    inline constexpr std::size_t MaxParameterBytes2D = 128;
    /** @} */

    /** @brief How a 2D pipeline's pixels combine with the target's, colour and alpha factors set apart (ADR-0027). */
    enum class BlendMode2D : std::uint8_t
    {
        /** @brief Straight alpha: RGB `SrcAlpha` / `OneMinusSrcAlpha`, alpha `One` / `OneMinusSrcAlpha`. */
        AlphaBlend,
        /** @brief Premultiplied alpha: RGB and alpha `One` / `OneMinusSrcAlpha`. */
        PremultipliedAlpha,
        /** @brief RGB `One` / `One`; alpha `Zero` / `One`, so the target's alpha is kept. */
        Additive,
        /** @brief No blending: the shader's colour and alpha are written. */
        Opaque,
    };

    /**
     * @brief The space a 2D pipeline blends in. `Linear` blends linear values and the target encodes on write; `Gamma`
     * (opt-in, for UIs that assume it, as stock ImGui does) turns encoding off so values pass through and blend in
     * sRGB. `Gamma` is allowed only on an 8-bit sRGB target; anywhere else it is a logged error and draws as `Linear`.
     */
    enum class BlendSpace2D : std::uint8_t
    {
        Linear,
        Gamma,
    };

    /** @brief The width of a 2D draw list's indices. */
    enum class IndexType2D : std::uint8_t
    {
        UInt16,
        UInt32,
    };

    /** @brief The type of one vertex attribute in a 2D draw list's vertex bytes. */
    enum class VertexFormat2D : std::uint8_t
    {
        Float,
        Float2,
        Float3,
        Float4,
        /** @brief Four bytes read as [0, 1] floats, such as an RGBA8 colour. */
        Unorm8x4,
    };

    /** @brief One vertex attribute: the shader location it feeds, its type, and its offset in a vertex. */
    struct VertexAttribute2D
    {
        std::uint32_t location = 0;
        VertexFormat2D format = VertexFormat2D::Float2;
        std::uint32_t offset = 0;

        friend bool operator==(const VertexAttribute2D&, const VertexAttribute2D&) = default;
    };

    /** @brief How a 2D draw list's vertex bytes are laid out: a vertex's size and its attributes. */
    struct VertexLayout2D
    {
        std::uint32_t stride = 0;
        std::vector<VertexAttribute2D> attributes;

        friend bool operator==(const VertexLayout2D&, const VertexLayout2D&) = default;
    };

    /**
     * @brief A 2D pipeline description (ADR-0027): a plain value saying how a 2D draw list's triangles are drawn. It
     * holds no renderer handle; the renderer makes a device pipeline from it, cached by value, so equal descriptions
     * share one.
     *
     * The renderer picks the shader's variant from it: a 2D shader that has the keys gets `INPUT_SRGB` with an `Srgb`
     * input colour space, `PREMULTIPLY` with `PremultipliedAlpha`, and `GAMMA` with the `Gamma` blend space (when the
     * target allows it). A shader's output is always in the convention its blend mode names, linear unless `Gamma`.
     */
    struct PipelineDescription2D
    {
        /** @brief A 2D shader (its set 0 from `Fenrir/2D.glsl`), built by `fenrir_add_shaders`; `builtin:2d` is one. */
        AssetHandle<Shader> shader;
        VertexLayout2D vertexLayout;
        IndexType2D indexType = IndexType2D::UInt16;
        BlendMode2D blendMode = BlendMode2D::AlphaBlend;
        /** @brief What the vertex colours and colour parameters are: sRGB-encoded (the default) or linear. */
        ColourSpace inputColourSpace = ColourSpace::Srgb;
        BlendSpace2D blendSpace = BlendSpace2D::Linear;

        friend bool operator==(const PipelineDescription2D&, const PipelineDescription2D&) = default;
    };

    /** @brief A 2D texture slot's default sampler: linear filtering, clamped to the edge, no anisotropy. */
    inline constexpr SamplerDescription LinearClamp2D{SamplerWrap::ClampToEdge, SamplerWrap::ClampToEdge,
                                                      SamplerFilter::Bilinear, 1};

    /**
     * @brief One texture a 2D command binds: an image (a camera's render target image is one) and its sampler. An
     * empty, stale or unsampleable image binds the shader's default texture for the slot.
     */
    struct TextureSlot2D
    {
        AssetHandle<Image> image;
        SamplerDescription sampler = LinearClamp2D;

        friend bool operator==(const TextureSlot2D&, const TextureSlot2D&) = default;
    };

    /** @brief A rectangle in local 2D coordinates: logical units from the top-left, Y down. */
    struct Rect2D
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;

        friend bool operator==(const Rect2D&, const Rect2D&) = default;
    };

    /** @brief One draw of a 2D draw list: a range of its indices, drawn with one of its pipelines. */
    struct Command2D
    {
        /** @brief The index of its pipeline in DrawList2D::pipelines. */
        std::uint32_t pipeline = 0;
        /**
         * @brief At most MaxTextureSlots2D: slot `i` binds the shader's `i`th texture of set 1, in binding order. A
         * texture the slots do not reach binds its default.
         */
        std::vector<TextureSlot2D> textures;
        /** @brief Outside this rectangle nothing is drawn; none clips only to the target. */
        std::optional<Rect2D> scissor;
        /** @brief The first index, counted in indices, and how many are drawn, three a triangle. */
        std::uint32_t indexOffset = 0;
        std::uint32_t indexCount = 0;
        /** @brief Added to each index before the vertex is read. */
        std::int32_t baseVertex = 0;
        /**
         * @brief At most MaxParameterBytes2D, written over the start of the shader's `MaterialParams` block in set 1,
         * laid out as its reflection says (std140); the rest of the block keeps its parameter defaults.
         */
        std::vector<std::byte> parameters;
    };

    /**
     * @brief A 2D draw list (ADR-0027): one batch of 2D drawing as plain data, which any UI library, painter or
     * language can fill: vertex and index bytes, a table of 2D pipeline descriptions, and commands in the order they
     * draw. Vertices and scissors are in local 2D coordinates, logical units from the top-left with Y down, the units
     * of FenrirInput's mouse. A draw list never says where it is shown; the 2D target decides.
     */
    struct DrawList2D
    {
        std::vector<std::byte> vertices;
        std::vector<std::byte> indices;
        std::vector<PipelineDescription2D> pipelines;
        std::vector<Command2D> commands;

        /** @brief The index of a pipeline equal to `pipeline`, added if the list has none. */
        std::uint32_t AddPipeline(const PipelineDescription2D& pipeline)
        {
            for (std::size_t i = 0; i < pipelines.size(); ++i)
            {
                if (pipelines[i] == pipeline)
                {
                    return static_cast<std::uint32_t>(i);
                }
            }
            pipelines.push_back(pipeline);
            return static_cast<std::uint32_t>(pipelines.size() - 1);
        }

        /** @brief Appends `values`' bytes to the vertex bytes; returns the first one's vertex number at `T`'s size. */
        template <typename T>
        std::uint32_t AppendVertices(std::span<const T> values)
        {
            const auto first = static_cast<std::uint32_t>(vertices.size() / sizeof(T));
            const std::span<const std::byte> bytes = std::as_bytes(values);
            vertices.insert(vertices.end(), bytes.begin(), bytes.end());
            return first;
        }

        /** @brief Appends `values`' bytes to the index bytes; returns the first one's index number at `T`'s size. */
        template <typename T>
        std::uint32_t AppendIndices(std::span<const T> values)
        {
            const auto first = static_cast<std::uint32_t>(indices.size() / sizeof(T));
            const std::span<const std::byte> bytes = std::as_bytes(values);
            indices.insert(indices.end(), bytes.begin(), bytes.end());
            return first;
        }
    };

    /**
     * @brief One vertex of the ImGui vertex layout, which `builtin:2d` reads: position, UV and an RGBA8 colour, red in
     * the lowest byte.
     */
    struct Vertex2D
    {
        Math::Vec2 position{0.0f};
        Math::Vec2 uv{0.0f};
        std::uint32_t colour = 0xFFFFFFFFu;
    };
    static_assert(sizeof(Vertex2D) == 20);

    /** @brief Vertex2D's layout: position at location 0, UV at 1, colour at 2. */
    [[nodiscard]] inline VertexLayout2D Vertex2DLayout()
    {
        return {sizeof(Vertex2D),
                {{0, VertexFormat2D::Float2, 0}, {1, VertexFormat2D::Float2, 8}, {2, VertexFormat2D::Unorm8x4, 16}}};
    }
} // namespace Fenrir
