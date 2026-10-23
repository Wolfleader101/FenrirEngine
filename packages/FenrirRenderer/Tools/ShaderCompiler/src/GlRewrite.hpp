#pragma once

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include "FenrirAssets/ShaderContainer.hpp"

namespace Fenrir::ShaderCompiler
{
    /** @brief Each binding's GL binding point, by its set and binding number. */
    using GlBindingMap = std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t>;

    /**
     * @brief The GL binding point of each of a program variant's bindings, by GlBindingOf: its set's slot plus its
     * index among the set's bindings of its kind, over every stage of the variant together.
     */
    GlBindingMap GlBindingsOf(const std::vector<ShaderBinding>& bindings);

    /**
     * @brief The GL copy of a stage's SPIR-V (spike lesson 1): every descriptor set zeroed, every binding flattened to
     * its point in `bindings` (from GlBindingsOf), and `VertexIndex` retargeted to GL's `VertexId` (they agree, base
     * vertex included). Everything else is copied unchanged.
     */
    std::vector<std::uint32_t> RewriteForGl(const std::vector<std::uint32_t>& spirv, const GlBindingMap& bindings);
} // namespace Fenrir::ShaderCompiler
