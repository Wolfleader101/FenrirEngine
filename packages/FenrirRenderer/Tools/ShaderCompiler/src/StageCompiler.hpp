#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "Diagnostics.hpp"
#include "FenrirAssets/ShaderContainer.hpp"

namespace Fenrir::ShaderCompiler
{
    /** @brief One stage of one variant, compiled. */
    struct CompiledStage
    {
        ShaderStage stage = ShaderStage::Vertex;
        /** @brief Its SPIR-V 1.0, Vulkan dialect, sets and bindings as written. */
        std::vector<std::uint32_t> spirv;
        /** @brief Its source after preprocessing with the variant's keys defined, where its pragmas are read. */
        std::string preprocessed;
    };

    /** @brief Starts and ends glslang's process-wide state around a run. */
    class GlslangProcess
    {
      public:
        GlslangProcess();
        ~GlslangProcess();
        GlslangProcess(const GlslangProcess&) = delete;
        GlslangProcess& operator=(const GlslangProcess&) = delete;
        GlslangProcess(GlslangProcess&&) = delete;
        GlslangProcess& operator=(GlslangProcess&&) = delete;
    };

    /**
     * @brief Compiles the stage in `path` as GLSL 460 with Vulkan semantics to SPIR-V 1.0, with each of `defines`
     * defined, resolving `#include` against the stage's own directory and then `includeDirs`. Enforces the engine's
     * rules glslang does not: an explicit `layout(set, binding)` on every resource, and no `gl_InstanceIndex`. Every
     * file it reads goes into `read`. Empty, with the errors reported, if the stage does not compile.
     */
    std::optional<CompiledStage> CompileStage(ShaderStage stage, const std::filesystem::path& path,
                                              const std::vector<std::string>& defines,
                                              const std::vector<std::filesystem::path>& includeDirs,
                                              std::set<std::filesystem::path>& read, Diagnostics& diagnostics);
} // namespace Fenrir::ShaderCompiler
