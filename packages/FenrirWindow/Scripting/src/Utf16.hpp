#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace Fenrir::Detail
{
    /**
     * @brief `text`, UTF-8, as UTF-16 (C#'s chars). A byte that does not start a valid UTF-8 sequence (a stray
     * continuation byte, a sequence cut short, an overlong form, a surrogate or a value past U+10FFFF) becomes U+FFFD,
     * and decoding goes on from the next byte.
     */
    inline std::u16string Utf8ToUtf16(std::string_view text)
    {
        constexpr char32_t replacement = 0xFFFD;
        std::u16string result;
        result.reserve(text.size());
        std::size_t index = 0;
        while (index < text.size())
        {
            const auto lead = static_cast<unsigned char>(text[index]);
            std::size_t length = 0;
            char32_t codepoint = 0;
            char32_t smallest = 0;
            if (lead < 0x80)
            {
                length = 1;
                codepoint = lead;
            }
            else if ((lead & 0xE0) == 0xC0)
            {
                length = 2;
                codepoint = lead & 0x1Fu;
                smallest = 0x80;
            }
            else if ((lead & 0xF0) == 0xE0)
            {
                length = 3;
                codepoint = lead & 0x0Fu;
                smallest = 0x800;
            }
            else if ((lead & 0xF8) == 0xF0)
            {
                length = 4;
                codepoint = lead & 0x07u;
                smallest = 0x10000;
            }
            bool valid = length != 0 && index + length <= text.size();
            for (std::size_t next = 1; valid && next < length; ++next)
            {
                const auto continuation = static_cast<unsigned char>(text[index + next]);
                valid = (continuation & 0xC0) == 0x80;
                codepoint = (codepoint << 6) | (continuation & 0x3Fu);
            }
            valid =
                valid && codepoint >= smallest && codepoint <= 0x10FFFF && (codepoint < 0xD800 || codepoint > 0xDFFF);
            if (!valid)
            {
                result.push_back(static_cast<char16_t>(replacement));
                ++index;
                continue;
            }
            if (codepoint < 0x10000)
            {
                result.push_back(static_cast<char16_t>(codepoint));
            }
            else
            {
                const char32_t offset = codepoint - 0x10000;
                result.push_back(static_cast<char16_t>(0xD800 + (offset >> 10)));
                result.push_back(static_cast<char16_t>(0xDC00 + (offset & 0x3FF)));
            }
            index += length;
        }
        return result;
    }
} // namespace Fenrir::Detail
