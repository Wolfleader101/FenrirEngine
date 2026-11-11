#include "Unicode.hpp"

#include <mutex>

#include <graphemebreak.h>
#include <linebreak.h>

namespace Fenrir::Detail
{
    namespace
    {
        // libunibreak's tables are set up once, before the first break is asked for.
        void InitialiseUnibreak()
        {
            static std::once_flag once;
            std::call_once(once, [] {
                init_linebreak();
                init_graphemebreak();
            });
        }

        const utf8_t* Bytes(std::string_view text)
        {
            return reinterpret_cast<const utf8_t*>(text.data());
        }
    } // namespace

    DecodedCodepoint DecodeUtf8(std::string_view text, std::size_t at)
    {
        const auto byte = [&](std::size_t i) { return static_cast<std::uint8_t>(text[i]); };
        const std::uint8_t lead = byte(at);
        if (lead < 0x80)
        {
            return {lead, 1};
        }
        std::uint32_t length = 0;
        char32_t codepoint = 0;
        if ((lead & 0xE0) == 0xC0)
        {
            length = 2;
            codepoint = lead & 0x1Fu;
        }
        else if ((lead & 0xF0) == 0xE0)
        {
            length = 3;
            codepoint = lead & 0x0Fu;
        }
        else if ((lead & 0xF8) == 0xF0)
        {
            length = 4;
            codepoint = lead & 0x07u;
        }
        else
        {
            return {U'\uFFFD', 1};
        }
        if (at + length > text.size())
        {
            return {U'\uFFFD', 1};
        }
        for (std::uint32_t i = 1; i < length; ++i)
        {
            const std::uint8_t next = byte(at + i);
            if ((next & 0xC0) != 0x80)
            {
                return {U'\uFFFD', 1};
            }
            codepoint = (codepoint << 6) | (next & 0x3Fu);
        }
        // Overlong forms, surrogates and values past U+10FFFF are malformed, as libunibreak reads them.
        constexpr char32_t smallest[] = {0, 0, 0x80, 0x800, 0x10000};
        if (codepoint < smallest[length] || (codepoint >= 0xD800 && codepoint <= 0xDFFF) || codepoint > 0x10FFFF)
        {
            return {U'\uFFFD', 1};
        }
        return {codepoint, length};
    }

    std::vector<std::uint32_t> GraphemeBoundaries(std::string_view text)
    {
        std::vector<std::uint32_t> boundaries{0};
        if (text.empty())
        {
            return boundaries;
        }
        InitialiseUnibreak();
        std::vector<char> breaks(text.size());
        set_graphemebreaks_utf8(Bytes(text), text.size(), nullptr, breaks.data());
        for (std::size_t i = 0; i + 1 < text.size(); ++i)
        {
            if (breaks[i] == GRAPHEMEBREAK_BREAK)
            {
                boundaries.push_back(static_cast<std::uint32_t>(i + 1));
            }
        }
        boundaries.push_back(static_cast<std::uint32_t>(text.size()));
        return boundaries;
    }

    std::vector<BreakAfter> LineBreaks(std::string_view text)
    {
        std::vector<BreakAfter> result(text.size(), BreakAfter::None);
        if (text.empty())
        {
            return result;
        }
        InitialiseUnibreak();
        std::vector<char> breaks(text.size());
        set_linebreaks_utf8(Bytes(text), text.size(), nullptr, breaks.data());
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            if (breaks[i] == LINEBREAK_MUSTBREAK)
            {
                result[i] = BreakAfter::Mandatory;
            }
            else if (breaks[i] == LINEBREAK_ALLOWBREAK)
            {
                result[i] = BreakAfter::Allowed;
            }
        }
        // The end of the text ends the last line, whatever UAX#14 says of its last character.
        result.back() = BreakAfter::Mandatory;
        return result;
    }
} // namespace Fenrir::Detail
