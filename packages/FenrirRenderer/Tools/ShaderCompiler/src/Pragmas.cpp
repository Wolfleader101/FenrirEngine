#include "Pragmas.hpp"

#include <cctype>
#include <charconv>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string_view>

namespace Fenrir::ShaderCompiler
{
    namespace
    {
        // The preprocessor joins a pragma's tokens without spaces ("#pragma fenrirdefault(a,1,2)"), which loses the
        // difference between "1 2" and "12". So a pragma is found in the preprocessed source, which says which survive
        // the variant's #defines, and parsed from the line it is written on, read from its file.
        class SourceLines
        {
          public:
            const std::string* Line(const std::string& file, int line)
            {
                auto [entry, added] = m_files.try_emplace(file);
                if (added)
                {
                    std::ifstream in(std::filesystem::path(file), std::ios::binary);
                    std::string text;
                    while (std::getline(in, text))
                    {
                        if (!text.empty() && text.back() == '\r')
                        {
                            text.pop_back();
                        }
                        entry->second.push_back(text);
                    }
                }
                const std::vector<std::string>& lines = entry->second;
                return line >= 1 && static_cast<std::size_t>(line) <= lines.size()
                           ? &lines[static_cast<std::size_t>(line - 1)]
                           : nullptr;
            }

          private:
            std::map<std::string, std::vector<std::string>> m_files;
        };

        // Splits a line into identifiers, numbers and single punctuation characters, stopping at a // comment.
        std::vector<std::string> Tokens(std::string_view line)
        {
            std::vector<std::string> tokens;
            std::size_t i = 0;
            const auto isWord = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; };
            while (i < line.size())
            {
                const char c = line[i];
                if (std::isspace(static_cast<unsigned char>(c)) != 0)
                {
                    ++i;
                }
                else if (line.substr(i, 2) == "//")
                {
                    break;
                }
                else if (isWord(c) || c == '.' ||
                         ((c == '-' || c == '+') && i + 1 < line.size() &&
                          (std::isdigit(static_cast<unsigned char>(line[i + 1])) != 0 || line[i + 1] == '.')))
                {
                    // A word or a number; a number keeps its sign, point and exponent sign.
                    const std::size_t start = i++;
                    const bool number = c != '_' && std::isalpha(static_cast<unsigned char>(c)) == 0;
                    while (i < line.size())
                    {
                        const char d = line[i];
                        const bool exponentSign =
                            number && (d == '-' || d == '+') && (line[i - 1] == 'e' || line[i - 1] == 'E');
                        if (!isWord(d) && d != '.' && !exponentSign)
                        {
                            break;
                        }
                        ++i;
                    }
                    tokens.emplace_back(line.substr(start, i - start));
                }
                else
                {
                    tokens.emplace_back(1, c);
                    ++i;
                }
            }
            return tokens;
        }

        bool IsIdentifier(const std::string& token)
        {
            return !token.empty() && (std::isalpha(static_cast<unsigned char>(token[0])) != 0 || token[0] == '_') &&
                   token.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") ==
                       std::string::npos;
        }

        constexpr std::string_view Grammar = "#pragma fenrir default(<name>, <value>, ...)";

        // Parses `# pragma fenrir default ( name , value {, value} )`. Empty name, with the error reported, when the
        // line is not that.
        DefaultPragma Parse(const std::vector<std::string>& tokens, const std::string& file, int line,
                            Diagnostics& diagnostics)
        {
            DefaultPragma pragma{file, line, {}, {}};
            const auto fail = [&](const std::string& why) {
                diagnostics.Error(file, line, std::format("malformed pragma: {}; it is written {}", why, Grammar));
                pragma.name.clear();
                return pragma;
            };
            // tokens[0..2] are "#", "pragma", "fenrir", checked by the caller.
            std::size_t at = 3;
            if (at >= tokens.size())
            {
                return fail("'#pragma fenrir' says nothing");
            }
            if (tokens[at] != "default")
            {
                return fail(std::format("'{}' is not a fenrir pragma (the one there is is 'default')", tokens[at]));
            }
            if (++at >= tokens.size() || tokens[at] != "(")
            {
                return fail("'default' is not followed by '('");
            }
            if (++at >= tokens.size() || !IsIdentifier(tokens[at]))
            {
                return fail("it does not start with the name of a member or a sampler");
            }
            pragma.name = tokens[at++];
            while (at < tokens.size() && tokens[at] == ",")
            {
                if (++at >= tokens.size() || tokens[at] == "," || tokens[at] == ")")
                {
                    return fail("a value is missing after a comma");
                }
                pragma.values.push_back(tokens[at++]);
            }
            if (pragma.values.empty())
            {
                return fail(std::format("'{}' is given no value", pragma.name));
            }
            if (at >= tokens.size() || tokens[at] != ")")
            {
                return fail(at < tokens.size() ? std::format("'{}' where a ',' or ')' should be", tokens[at])
                                               : std::string("it does not end with ')'"));
            }
            if (++at != tokens.size())
            {
                return fail(std::format("'{}' follows the closing ')'", tokens[at]));
            }
            for (const std::string& value : pragma.values)
            {
                float number = 0.0f;
                if (!ParseFloatValue(value, number) && value != "white" && value != "black" && value != "normal")
                {
                    return fail(std::format("'{}' is neither a float literal nor white, black or normal", value));
                }
            }
            return pragma;
        }

        // A `#line N "file"` or `#line N` directive: the line the next line has, and maybe its file.
        bool ReadLineDirective(std::string_view text, int& line, std::string& file)
        {
            std::size_t at = text.find_first_not_of(" \t");
            if (at == std::string_view::npos || text.substr(at, 5) != "#line")
            {
                return false;
            }
            at = text.find_first_not_of(" \t", at + 5);
            if (at == std::string_view::npos)
            {
                return false;
            }
            const std::string_view rest = text.substr(at);
            int number = 0;
            if (std::from_chars(std::to_address(rest.begin()), std::to_address(rest.end()), number).ec != std::errc{})
            {
                return false;
            }
            line = number;
            const std::size_t open = rest.find('"');
            const std::size_t close = open == std::string_view::npos ? open : rest.find('"', open + 1);
            if (close != std::string_view::npos)
            {
                file = std::string(rest.substr(open + 1, close - open - 1));
            }
            return true;
        }

        bool IsFenrirPragma(const std::vector<std::string>& tokens)
        {
            return tokens.size() >= 3 && tokens[0] == "#" && tokens[1] == "pragma" && tokens[2] == "fenrir";
        }
    } // namespace

    bool ParseFloatValue(const std::string& text, float& value)
    {
        std::string_view digits = text;
        if (!digits.empty() && (digits.back() == 'f' || digits.back() == 'F'))
        {
            digits.remove_suffix(1);
        }
        if (!digits.empty() && digits.front() == '+')
        {
            digits.remove_prefix(1);
        }
        if (digits.empty() || digits.find_first_not_of("0123456789.eE+-") != std::string_view::npos)
        {
            return false;
        }
        const auto [end, error] = std::from_chars(std::to_address(digits.begin()), std::to_address(digits.end()), value,
                                                  std::chars_format::general);
        return error == std::errc{} && end == std::to_address(digits.end());
    }

    std::vector<DefaultPragma> ReadDefaultPragmas(const std::string& preprocessed, const std::string& mainFile,
                                                  Diagnostics& diagnostics)
    {
        std::vector<DefaultPragma> pragmas;
        SourceLines sources;
        std::string file = mainFile;
        int line = 1;
        std::istringstream lines(preprocessed);
        std::string text;
        while (std::getline(lines, text))
        {
            int nextLine = 0;
            if (ReadLineDirective(text, nextLine, file))
            {
                line = nextLine;
                continue;
            }
            const std::size_t at = text.find_first_not_of(" \t");
            if (at != std::string::npos && text.compare(at, 14, "#pragma fenrir") == 0)
            {
                const std::string* written = sources.Line(file, line);
                const std::vector<std::string> tokens = Tokens(written != nullptr ? *written : std::string_view());
                // Someone else's pragma that only starts with "fenrir" ("#pragma fenrirish") is left alone.
                const bool otherPragma =
                    tokens.size() >= 3 && tokens[0] == "#" && tokens[1] == "pragma" && tokens[2] != "fenrir";
                if (otherPragma)
                {
                    ++line;
                    continue;
                }
                if (!IsFenrirPragma(tokens))
                {
                    // Not on one line as written (a line continuation, say), so it cannot be read exactly.
                    diagnostics.Error(file, line,
                                      std::format("malformed pragma: write it on one line, as {}", Grammar));
                }
                else if (DefaultPragma pragma = Parse(tokens, file, line, diagnostics); !pragma.name.empty())
                {
                    pragmas.push_back(std::move(pragma));
                }
            }
            ++line;
        }
        return pragmas;
    }
} // namespace Fenrir::ShaderCompiler
