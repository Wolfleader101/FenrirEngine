#pragma once

#include <cstdint>

namespace Fenrir
{
    /**
     * @brief The most bindings of one resource kind a binding set may hold (spike lesson 2). FenrirShaderCompiler
     * refuses a shader with more.
     */
    inline constexpr std::uint32_t ShaderBindingsPerSet = 8;

    /**
     * @brief The flattening formula: the one GL binding point `layout(set, binding)` becomes in a shader container's GL
     * SPIR-V, where every descriptor set is 0 (GL's SPIR-V path has no sets).
     *
     * GL binding = set x 8 + the binding's index among its set's bindings of the same kind, in binding order. Uniform
     * buffers, storage buffers and textures each have their own binding points in GL, so each kind is counted on its
     * own: a set with a `MaterialParams` block at binding 0 and textures at bindings 1 to 5 has uniform buffer index 0
     * and texture indices 0 to 4, so set 2's textures are texture units 16 to 20. Four sets of eight are binding points
     * 0 to 31, inside GL 4.6's minimums of 80 combined texture units and 72 uniform buffer bindings; a program still
     * samples at most 16 textures in its fragment stage. The index is taken over all the stages of a program variant
     * together, so a binding has one GL binding point in each of them.
     *
     * *Amended 2026-10-23 (renderer ticket 13):* it was set x 4 + binding, which left no room in one set for a block
     * and four textures, as the built-in materials need.
     *
     * *Amended 2026-10-31 (lighting ticket 05):* it was set x 4 + the index, which left no room for `builtin:lit`'s
     * fifth texture, `occlusionTexture`, nor for the frame textures shadows and ambient occlusion add to set 0.
     * Storage buffers reach binding 31 as well, past GL's minimum of 8 shader storage bindings but inside what desktop
     * drivers give.
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
