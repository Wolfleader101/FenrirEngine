#pragma once

#include <cstdio>
#include <format>
#include <set>
#include <string>
#include <string_view>

namespace Fenrir::ShaderCompiler
{
    /**
     * @brief Collects the build errors of one run and prints each to stderr as it comes, as `file:line: error: ...`
     * (or `file: error: ...` without a line), the form compilers and IDEs recognise. Any error fails the run. An error
     * every variant meets is printed once.
     */
    class Diagnostics
    {
      public:
        /** @brief Reports an error at `line` of `file`. */
        void Error(std::string_view file, int line, std::string_view message)
        {
            Print(std::format("{}:{}: error: {}\n", file, line, message));
        }

        /** @brief Reports an error about `file` as a whole. */
        void Error(std::string_view file, std::string_view message)
        {
            Print(std::format("{}: error: {}\n", file, message));
        }

        /** @brief Reports glslang's own log, which already names files and lines. */
        void Log(std::string_view log)
        {
            std::string text(log);
            if (text.empty() || text.back() != '\n')
            {
                text += '\n';
            }
            Print(std::move(text));
        }

        /** @brief How many errors have been reported so far, repeats included. */
        [[nodiscard]] int ErrorCount() const noexcept
        {
            return m_errors;
        }

      private:
        void Print(std::string text)
        {
            ++m_errors;
            if (m_printed.insert(text).second)
            {
                std::fputs(text.c_str(), stderr);
            }
        }

        int m_errors = 0;
        std::set<std::string> m_printed;
    };
} // namespace Fenrir::ShaderCompiler
