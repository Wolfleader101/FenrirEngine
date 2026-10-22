#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>

#include "Program.hpp"

namespace Fenrir::ShaderCompiler
{
    /** @brief What one run of the tool is asked to do. */
    struct Arguments
    {
        ProgramSource program;
        /** @brief Where the shader container is written. */
        std::filesystem::path output;
        /** @brief Where the depfile is written, if anywhere. */
        std::filesystem::path depfile;
        /** @brief Where the embedding header is written, if anywhere. */
        std::filesystem::path header;
    };

    /** @brief How the tool is run, for an error message. */
    inline constexpr const char* Usage =
        "usage: FenrirShaderCompiler --name <program> --vertex <file> --fragment <file> --output <file.fshader>\n"
        "           [--key <define>]... [--include <dir>]... [--depfile <file>] [--header <file.hpp>]\n";

    /** @brief The run `arguments` (without the program name) asks for, or empty, with the problem printed. */
    std::optional<Arguments> ParseArguments(std::span<const std::string> arguments);
} // namespace Fenrir::ShaderCompiler
