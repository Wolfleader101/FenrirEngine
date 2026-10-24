#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Fenrir
{
    /** @brief The pipeline stage a piece of SPIR-V runs in. */
    enum class ShaderStage : std::uint8_t
    {
        Vertex,
        Fragment,
    };

    /** @brief What kind of resource a binding holds. Each kind is counted separately when bindings are flattened. */
    enum class ShaderBindingKind : std::uint8_t
    {
        /** @brief A uniform block (std140). */
        UniformBuffer,
        /** @brief A storage block (`buffer`). */
        StorageBuffer,
        /** @brief A combined image sampler (`sampler2D`, `samplerCube`...). */
        Texture,
    };

    /**
     * @brief The type of a vertex input or a block member, as reflected. Only `Float` to `Vec4` can be material
     * parameters; the rest are reflected so the layout is complete.
     */
    enum class ShaderValueType : std::uint8_t
    {
        Float,
        Vec2,
        Vec3,
        Vec4,
        Int,
        IVec2,
        IVec3,
        IVec4,
        UInt,
        UVec2,
        UVec3,
        UVec4,
        /** @brief A matrix of any size. */
        Matrix,
        /** @brief An array of anything. */
        Array,
        /** @brief A struct. */
        Struct,
        /** @brief Anything else (a bool, a double...). */
        Other,
    };

    /** @brief The type of a material parameter: a `float` to `vec4` member of `MaterialParams`, or a texture. */
    enum class ShaderParameterType : std::uint8_t
    {
        Float,
        Vector2,
        Vector3,
        Vector4,
        Texture,
    };

    /** @brief Which of the renderer's 1x1 default textures a texture parameter binds when a material sets none. */
    enum class DefaultTexture : std::uint8_t
    {
        White,
        Black,
        /** @brief The flat tangent-space normal (0.5, 0.5, 1), linear. */
        Normal,
    };

    /**
     * @brief One material parameter of a shader with its parameter default: what `#pragma fenrir default` declared,
     * else zero (a texture: white).
     */
    struct ShaderParameter
    {
        /** @brief The member's or the sampler's name in the GLSL. */
        std::string name;
        ShaderParameterType type = ShaderParameterType::Float;
        /** @brief A number parameter's default: one value per component, the rest zero. Unused for a texture. */
        std::array<float, 4> value{};
        /** @brief A texture parameter's default. Unused for a number. */
        DefaultTexture texture = DefaultTexture::White;

        friend bool operator==(const ShaderParameter&, const ShaderParameter&) = default;
    };

    /** @brief One vertex input of a program's vertex stage. */
    struct ShaderVertexInput
    {
        std::uint32_t location = 0;
        std::string name;
        ShaderValueType type = ShaderValueType::Float;

        friend bool operator==(const ShaderVertexInput&, const ShaderVertexInput&) = default;
    };

    /**
     * @brief One resource binding of a program, at its unflattened `layout(set, binding)`. A block is named by its
     * block name (`MaterialParams`), a texture by its variable name (`baseColourTexture`).
     */
    struct ShaderBinding
    {
        std::uint32_t set = 0;
        std::uint32_t binding = 0;
        ShaderBindingKind kind = ShaderBindingKind::UniformBuffer;
        std::string name;
        /** @brief A block's size in bytes, padded as the layout rules pad it; zero for a texture. */
        std::uint32_t size = 0;

        friend bool operator==(const ShaderBinding&, const ShaderBinding&) = default;
    };

    /** @brief One member of the `MaterialParams` block, as std140 lays it out. */
    struct ShaderBlockMember
    {
        std::string name;
        ShaderValueType type = ShaderValueType::Float;
        /** @brief Its byte offset in the block. */
        std::uint32_t offset = 0;
        /** @brief Its size in bytes (an array's: its whole stride times its length). */
        std::uint32_t size = 0;

        friend bool operator==(const ShaderBlockMember&, const ShaderBlockMember&) = default;
    };

    /** @brief One stage of a variant: its SPIR-V for OpenGL and its SPIR-V as compiled. */
    struct ShaderStageCode
    {
        ShaderStage stage = ShaderStage::Vertex;
        /**
         * @brief The GL copy: descriptor sets zeroed, bindings flattened by GlBindingOf, `VertexIndex` retargeted to
         * `VertexId`. What the OpenGL backend specialises.
         */
        std::vector<std::uint32_t> glSpirv;
        /** @brief The SPIR-V as glslang made it, sets and bindings as written, kept for a later Vulkan backend. */
        std::vector<std::uint32_t> spirv;

        friend bool operator==(const ShaderStageCode&, const ShaderStageCode&) = default;
    };

    /** @brief One shader variant: one combination of the program's `#define` keys, compiled. */
    struct ShaderVariant
    {
        /** @brief The keys this variant defines, in the order the program lists them. Empty for the base variant. */
        std::vector<std::string> keys;
        /** @brief Its stages, vertex first. */
        std::vector<ShaderStageCode> stages;
        /** @brief The vertex stage's inputs, by location. */
        std::vector<ShaderVertexInput> vertexInputs;
        /** @brief Every resource binding of every stage, by set then binding. */
        std::vector<ShaderBinding> bindings;
        /** @brief The binding of the `MaterialParams` block in the material set, if the program declares one. */
        std::optional<std::uint32_t> materialBlockBinding;
        /** @brief The `MaterialParams` block's members, by offset; empty without the block. */
        std::vector<ShaderBlockMember> materialMembers;
        /** @brief The material parameters with their defaults: the block's numeric members, then the textures. */
        std::vector<ShaderParameter> parameters;
        /**
         * @brief The `MaterialParams` block packed with its parameter defaults at its std140 offsets, as many bytes as
         * the block; empty without the block. The renderer copies it, then writes a material's overrides over it.
         */
        std::vector<std::byte> defaultBlock;

        friend bool operator==(const ShaderVariant&, const ShaderVariant&) = default;
    };

    /**
     * @brief A shader container (ADR-0018): everything FenrirShaderCompiler builds for one program, every variant with
     * its code, reflection and parameter defaults. Written by the tool, read by the renderer and FenrirAssets.
     */
    struct ShaderContainer
    {
        /** @brief The program's name, as `fenrir_add_shaders` was given it. */
        std::string name;
        /**
         * @brief The binding set holding the material: 2 for a mesh shader, 1 for a 2D shader (whose set 0 is the 2D
         * target's, from `Fenrir/2D.glsl`).
         */
        std::uint32_t materialSet = 2;
        /** @brief The program's variant keys, in the order `fenrir_add_shaders` named them. */
        std::vector<std::string> keys;
        /** @brief Every combination of the keys: variant `i` defines key `k` when bit `k` of `i` is set. */
        std::vector<ShaderVariant> variants;

        friend bool operator==(const ShaderContainer&, const ShaderContainer&) = default;

        /**
         * @brief The variant defining exactly those of `wanted` that the program has. Keys it lacks are ignored, so a
         * renderer can ask for every key it knows. Null only for a container with no variants.
         */
        [[nodiscard]] const ShaderVariant* FindVariant(std::span<const std::string_view> wanted) const;
    };

    /** @brief The first four bytes of every shader container. */
    inline constexpr std::array<std::byte, 4> ShaderContainerMagic = {std::byte{'F'}, std::byte{'S'}, std::byte{'H'},
                                                                      std::byte{'C'}};

    /** @brief The container format this build writes and reads. A container of any other version is refused. */
    inline constexpr std::uint32_t ShaderContainerVersion = 1;

    /** @brief What ReadShaderContainer gives back: the container, or why the bytes are not one. */
    struct ShaderContainerResult
    {
        /** @brief The container; empty when the bytes could not be read. */
        std::optional<ShaderContainer> container;
        /** @brief Why the bytes could not be read; empty when they were. */
        std::string diagnostic;

        /** @brief Whether the bytes were read. */
        [[nodiscard]] bool Ok() const noexcept
        {
            return container.has_value();
        }
    };

    /**
     * @brief Reads a shader container from the bytes FenrirShaderCompiler wrote. Bytes with the wrong magic, another
     * format version, or that end early or run on are refused with a diagnostic saying which.
     */
    [[nodiscard]] ShaderContainerResult ReadShaderContainer(std::span<const std::byte> bytes);

    /** @brief The bytes of `container` in the current format, as ReadShaderContainer reads them. */
    [[nodiscard]] std::vector<std::byte> WriteShaderContainer(const ShaderContainer& container);

    /**
     * @brief A program's material parameters across its variants: each name once, with the type and default of the
     * first variant (by index, the base variant first) that has it. What an editor or a material reads.
     */
    [[nodiscard]] std::vector<ShaderParameter> CollectShaderParameters(const ShaderContainer& container);
} // namespace Fenrir
