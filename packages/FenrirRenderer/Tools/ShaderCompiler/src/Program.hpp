#pragma once

#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "Diagnostics.hpp"
#include "FenrirAssets/ShaderContainer.hpp"

namespace Fenrir::ShaderCompiler
{
    /** @brief One shader program as `fenrir_add_shaders` names it. */
    struct ProgramSource
    {
        std::string name;
        std::filesystem::path vertex;
        std::filesystem::path fragment;
        /** @brief Its variant keys: every combination is built. */
        std::vector<std::string> keys;
        /** @brief Searched for `#include`, in order, after the including file's own directory. */
        std::vector<std::filesystem::path> includeDirs;
    };

    /**
     * @brief Builds every variant of `program` (spec: "Shaders and FenrirShaderCompiler", steps 1 to 5): compiles each
     * stage, writes its GL copy, reflects it, and reads and checks its parameter defaults. Every file read goes into
     * `read`, for the depfile. Empty, with every error reported, if anything fails.
     */
    std::optional<ShaderContainer> BuildProgram(const ProgramSource& program, std::set<std::filesystem::path>& read,
                                                Diagnostics& diagnostics);
} // namespace Fenrir::ShaderCompiler
