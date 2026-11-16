#pragma once

#include <cstdint>
#include <string>

namespace Fenrir::Detail
{
    /** @brief `codepoint` as UTF-8, appended to `out`: one to four bytes. */
    inline void AppendUtf8(std::string& out, std::uint32_t codepoint)
    {
        const auto byte = [&](std::uint32_t value) { out.push_back(static_cast<char>(value)); };
        if (codepoint < 0x80u)
        {
            byte(codepoint);
        }
        else if (codepoint < 0x800u)
        {
            byte(0xC0u | (codepoint >> 6u));
            byte(0x80u | (codepoint & 0x3Fu));
        }
        else if (codepoint < 0x10000u)
        {
            byte(0xE0u | (codepoint >> 12u));
            byte(0x80u | ((codepoint >> 6u) & 0x3Fu));
            byte(0x80u | (codepoint & 0x3Fu));
        }
        else
        {
            byte(0xF0u | (codepoint >> 18u));
            byte(0x80u | ((codepoint >> 12u) & 0x3Fu));
            byte(0x80u | ((codepoint >> 6u) & 0x3Fu));
            byte(0x80u | (codepoint & 0x3Fu));
        }
    }
} // namespace Fenrir::Detail
