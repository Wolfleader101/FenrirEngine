#include "Arguments.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace Fenrir::ShaderCompiler
{
    namespace
    {
        bool IsIdentifier(const std::string& text)
        {
            return !text.empty() && std::isdigit(static_cast<unsigned char>(text[0])) == 0 &&
                   std::ranges::all_of(
                       text, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; });
        }

        bool Fail(const std::string& problem)
        {
            std::fprintf(stderr, "FenrirShaderCompiler: error: %s\n%s", problem.c_str(), Usage);
            return false;
        }
    } // namespace

    std::optional<Arguments> ParseArguments(std::span<const std::string> arguments)
    {
        Arguments parsed;
        for (std::size_t i = 0; i < arguments.size(); i += 2)
        {
            const std::string& flag = arguments[i];
            if (i + 1 >= arguments.size())
            {
                Fail(flag + " needs a value");
                return std::nullopt;
            }
            const std::string& value = arguments[i + 1];
            if (flag == "--name")
            {
                parsed.program.name = value;
            }
            else if (flag == "--vertex")
            {
                parsed.program.vertex = value;
            }
            else if (flag == "--fragment")
            {
                parsed.program.fragment = value;
            }
            else if (flag == "--key")
            {
                if (!IsIdentifier(value) || std::ranges::find(parsed.program.keys, value) != parsed.program.keys.end())
                {
                    Fail("the variant key '" + value + "' is not an identifier, or is named twice");
                    return std::nullopt;
                }
                parsed.program.keys.push_back(value);
            }
            else if (flag == "--include")
            {
                parsed.program.includeDirs.emplace_back(value);
            }
            else if (flag == "--output")
            {
                parsed.output = value;
            }
            else if (flag == "--depfile")
            {
                parsed.depfile = value;
            }
            else if (flag == "--header")
            {
                parsed.header = value;
            }
            else
            {
                Fail("unknown option " + flag);
                return std::nullopt;
            }
        }
        if (parsed.program.name.empty() || parsed.program.vertex.empty() || parsed.program.fragment.empty() ||
            parsed.output.empty())
        {
            Fail("--name, --vertex, --fragment and --output are required");
            return std::nullopt;
        }
        if (!parsed.header.empty() && !IsIdentifier(parsed.program.name))
        {
            Fail("the program name '" + parsed.program.name + "' must be a C++ identifier to be embedded");
            return std::nullopt;
        }
        return parsed;
    }
} // namespace Fenrir::ShaderCompiler
