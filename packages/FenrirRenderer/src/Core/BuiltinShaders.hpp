#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Fenrir::Render
{
    /** @brief One of the renderer's built-in shader programs, embedded in it as a shader container (ADR-0018). */
    enum class BuiltinProgram : std::uint8_t
    {
        /** @brief `builtin:unlit`: base colour times vertex colour. */
        Unlit,
        /** @brief `builtin:lit`: metallic-roughness GGX through `Fenrir/Lighting.glsl`. */
        Lit,
        /** @brief `builtin:fallback`: the fallback material's flat magenta. */
        Fallback,
        /** @brief `builtin:background`: the Background pass at depth 0, a colour or (its CUBEMAP variant) a cubemap. */
        Background,
        /** @brief `builtin:output`: the Output pass, SceneColor clamped into the backbuffer. */
        Output,
        /** @brief `builtin:debug-lines`: debug lines, expanded to screen-space quads of their thickness. */
        DebugLines,
        /** @brief `builtin:debug-triangles`: solid debug shapes, alpha-blended. */
        DebugTriangles,
        /** @brief `builtin:debug-text`: debug text from the debug font's signed-distance-field atlas. */
        DebugText,
        /** @brief `builtin:2d`: 2D draw lists on the ImGui vertex layout, textured and vertex-coloured (ADR-0027). */
        Draw2D,
        /** @brief `builtin:panorama`: a panorama sky's image drawn into one face and mip of its cube texture. */
        Panorama,
        /** @brief `builtin:sky-irradiance`: the Sky cube projected onto L2 spherical harmonics. */
        SkyIrradiance,
        /** @brief `builtin:sky-specular`: the Sky cube prefiltered with GGX into one face and mip of its specular cube.
         */
        SkySpecular,
        /** @brief `builtin:brdf-lut`: the split sum's BRDF lookup table. */
        BrdfLut,
    };

    /** @brief A built-in program's `builtin:` name and its embedded container's bytes. */
    struct BuiltinShader
    {
        BuiltinProgram program = BuiltinProgram::Unlit;
        std::string_view name;
        std::span<const std::byte> bytes;
    };

    /** @brief Every built-in program, in BuiltinProgram's order, for the facade to register in the asset store. */
    [[nodiscard]] std::span<const BuiltinShader> BuiltinShaders();

    /** @brief One built-in program's embedded container. */
    [[nodiscard]] const BuiltinShader& GetBuiltinShader(BuiltinProgram program);
} // namespace Fenrir::Render
