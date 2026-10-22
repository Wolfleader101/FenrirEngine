#pragma once

#include <cstdint>

namespace Fenrir
{
    /**
     * @brief The most bindings of one resource kind a binding set may hold (spike lesson 2). FenrirShaderCompiler
     * refuses a shader with more.
     */
    inline constexpr std::uint32_t ShaderBindingsPerSet = 4;

    /**
     * @brief The flattening formula: the one GL binding point `layout(set, binding)` becomes in a shader container's GL
     * SPIR-V, where every descriptor set is 0 (GL's SPIR-V path has no sets).
     *
     * GL binding = set x 4 + binding, counted separately per resource kind: uniform buffers, storage buffers and
     * textures each have their own binding points in GL, so set 1 binding 0 is uniform buffer binding 4 and also,
     * for a texture, texture unit 4. Four per set keeps four sets of textures inside GL's minimum of 16 fragment
     * texture units. FenrirShaderCompiler writes the GL copy with it, and the OpenGL backend binds with it; this is the
     * only place it is written down.
     */
    [[nodiscard]] constexpr std::uint32_t GlBindingOf(std::uint32_t set, std::uint32_t binding) noexcept
    {
        return set * ShaderBindingsPerSet + binding;
    }
} // namespace Fenrir
