#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

// UAX#29 segments of plain UTF-8 text, outside any paragraph (libunibreak's grapheme and word boundaries): what a text
// entry moves and deletes by when the text it shows is not the text it holds, as a password's bullets are not.
namespace Fenrir
{
    /** @brief A byte range [`begin`, `end`) of UTF-8 text. */
    struct TextRange
    {
        std::uint32_t begin = 0;
        std::uint32_t end = 0;

        friend bool operator==(const TextRange&, const TextRange&) noexcept = default;
    };

    /** @brief The UAX#29 grapheme boundaries of `utf8`, ascending byte offsets from 0 to its size. */
    [[nodiscard]] std::vector<std::uint32_t> GraphemeBoundaries(std::string_view utf8);

    /**
     * @brief The words of `utf8`, ascending: the segments between its UAX#29 word boundaries that hold a letter, a
     * digit or an ideograph. A segment of spaces or punctuation (ASCII's, Latin-1's, General Punctuation's, and CJK
     * Symbols and Punctuation's) is not one. Malformed UTF-8 reads as U+FFFD, which counts as a word's.
     */
    [[nodiscard]] std::vector<TextRange> Words(std::string_view utf8);
} // namespace Fenrir
