#pragma once

#include <cstdint>
#include <vector>

namespace Fenrir::ShaderCompiler
{
    /**
     * @brief The GL copy of a stage's SPIR-V (spike lesson 1): every descriptor set zeroed, every binding flattened by
     * GlBindingOf, and `VertexIndex` retargeted to GL's `VertexId` (they agree, base vertex included). Everything else
     * is copied unchanged.
     */
    std::vector<std::uint32_t> RewriteForGl(const std::vector<std::uint32_t>& spirv);
} // namespace Fenrir::ShaderCompiler
