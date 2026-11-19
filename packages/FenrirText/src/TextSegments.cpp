#include "FenrirText/TextSegments.hpp"

#include "Unicode.hpp"

namespace Fenrir
{
    namespace
    {
        /**
         * @brief Whether `codepoint` is a space or punctuation, which starts no word: ASCII's and Latin-1's symbols
         * (not their letters), the Unicode spaces, General Punctuation, CJK Symbols and Punctuation (not its iteration
         * and ideographic number marks) and the fullwidth forms of ASCII's punctuation.
         */
        constexpr bool IsSpaceOrPunctuation(char32_t codepoint) noexcept
        {
            if (codepoint < 0x80)
            {
                const bool alphanumeric = (codepoint >= U'0' && codepoint <= U'9') ||
                                          (codepoint >= U'A' && codepoint <= U'Z') ||
                                          (codepoint >= U'a' && codepoint <= U'z');
                return !alphanumeric;
            }
            if (codepoint <= 0xBF)
            {
                // ª, µ and º are letters.
                return codepoint != 0xAA && codepoint != 0xB5 && codepoint != 0xBA;
            }
            return codepoint == 0xD7 || codepoint == 0xF7 || codepoint == 0x1680 ||
                   (codepoint >= 0x2000 && codepoint <= 0x206F) ||
                   (codepoint >= 0x3000 && codepoint <= 0x303F && (codepoint < 0x3005 || codepoint > 0x3007)) ||
                   (codepoint >= 0xFF01 && codepoint <= 0xFF0F) || (codepoint >= 0xFF1A && codepoint <= 0xFF20) ||
                   (codepoint >= 0xFF3B && codepoint <= 0xFF40) || (codepoint >= 0xFF5B && codepoint <= 0xFF65);
        }
    } // namespace

    std::vector<std::uint32_t> GraphemeBoundaries(std::string_view utf8)
    {
        return Detail::GraphemeBoundaries(utf8);
    }

    std::vector<TextRange> Words(std::string_view utf8)
    {
        std::vector<TextRange> words;
        const std::vector<std::uint32_t> boundaries = Detail::WordBoundaries(utf8);
        for (std::size_t i = 0; i + 1 < boundaries.size(); ++i)
        {
            // A segment's first codepoint says what it is: UAX#29 keeps a word's marks and inner apostrophes with it,
            // and a run of spaces or punctuation apart from it.
            if (!IsSpaceOrPunctuation(Detail::DecodeUtf8(utf8, boundaries[i]).codepoint))
            {
                words.push_back({boundaries[i], boundaries[i + 1]});
            }
        }
        return words;
    }
} // namespace Fenrir
