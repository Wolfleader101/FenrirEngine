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
     * GL binding = set x 4 + the binding's index among its set's bindings of the same kind, in binding order. Uniform
     * buffers, storage buffers and textures each have their own binding points in GL, so each kind is counted on its
     * own: a set with a `MaterialParams` block at binding 0 and textures at bindings 1 to 4 has uniform buffer index 0
     * and texture indices 0 to 3, so set 2's textures are texture units 8 to 11. Four per set keeps four sets of
     * textures inside GL's minimum of 16 fragment texture units. The index is taken over all the stages of a program
     * variant together, so a binding has one GL binding point in each of them.
     *
     * *Amended 2026-10-23 (renderer ticket 13):* it was set x 4 + binding, which left no room in one set for a block
     * and four textures, as the built-in materials need.
     *
     * FenrirShaderCompiler writes the GL copy with it, and the OpenGL backend binds with it, taking the index from the
     * binding group's layout; this is the only place it is written down.
     *
     * @param set The binding's descriptor set.
     * @param indexInKind How many bindings of the same kind in the same set have a lower binding number.
     */
    [[nodiscard]] constexpr std::uint32_t GlBindingOf(std::uint32_t set, std::uint32_t indexInKind) noexcept
    {
        return set * ShaderBindingsPerSet + indexInKind;
    }
} // namespace Fenrir
