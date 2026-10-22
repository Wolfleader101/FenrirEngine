#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Diagnostics.hpp"
#include "FenrirAssets/ShaderContainer.hpp"

namespace Fenrir::ShaderCompiler
{
    /** @brief One binding of one stage, with a uniform block's members. */
    struct ReflectedBinding
    {
        ShaderBinding binding;
        /** @brief A uniform block's members, by offset; empty for anything else. */
        std::vector<ShaderBlockMember> members;
    };

    /** @brief What SPIRV-Reflect finds in one stage's SPIR-V. */
    struct StageReflection
    {
        /** @brief The vertex stage's inputs, by location, built-ins left out; empty for other stages. */
        std::vector<ShaderVertexInput> vertexInputs;
        /** @brief Every resource binding, by set then binding. */
        std::vector<ReflectedBinding> bindings;
    };

    /**
     * @brief Reflects one stage's SPIR-V. A resource kind the engine does not bind (a storage image, a separate
     * sampler...) and a binding past ShaderBindingsPerSet are reported against `file`.
     */
    StageReflection ReflectStage(ShaderStage stage, const std::vector<std::uint32_t>& spirv, const std::string& file,
                                 Diagnostics& diagnostics);
} // namespace Fenrir::ShaderCompiler
