#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Fenrir::Detail
{
    /** @brief One code point read from UTF-8, and how many bytes it took. */
    struct DecodedCodepoint
    {
        char32_t codepoint = 0;
        std::uint32_t length = 1;
    };

    /** @brief The code point starting at byte `at` of `text`; a malformed sequence reads as U+FFFD, one byte long. */
    [[nodiscard]] DecodedCodepoint DecodeUtf8(std::string_view text, std::size_t at);

    /** @brief The UAX#29 grapheme boundaries of `text`, as byte offsets from 0 to its size (libunibreak). */
    [[nodiscard]] std::vector<std::uint32_t> GraphemeBoundaries(std::string_view text);

    /** @brief The UAX#29 word boundaries of `text`, as byte offsets from 0 to its size (libunibreak). */
    [[nodiscard]] std::vector<std::uint32_t> WordBoundaries(std::string_view text);

    /** @brief What UAX#14 allows after a byte of text. */
    enum class BreakAfter : std::uint8_t
    {
        /** @brief No break: inside a code point, or a break that UAX#14 forbids. */
        None,
        Allowed,
        /** @brief A line must end here (after a newline, and at the end of the text). */
        Mandatory,
    };

    /** @brief For each byte of `text`, whether a line may or must break after it (libunibreak). */
    [[nodiscard]] std::vector<BreakAfter> LineBreaks(std::string_view text);

    /** @brief Whether `codepoint` is a space, tab or line ending, which white space rules collapse. */
    [[nodiscard]] constexpr bool IsCollapsibleSpace(char32_t codepoint) noexcept
    {
        return codepoint == U' ' || codepoint == U'\t' || codepoint == U'\n' || codepoint == U'\r' ||
               codepoint == U'\f';
    }

    /**
     * @brief Whether `codepoint` is one a face need not have for its grapheme to be drawn in that face: joiners,
     * marks of direction, variation selectors and the other default-ignorable code points fonts often lack.
     */
    [[nodiscard]] constexpr bool IsDefaultIgnorable(char32_t codepoint) noexcept
    {
        return codepoint == 0x00AD || codepoint == 0x034F || (codepoint >= 0x200B && codepoint <= 0x200F) ||
               (codepoint >= 0x202A && codepoint <= 0x202E) || (codepoint >= 0x2060 && codepoint <= 0x206F) ||
               (codepoint >= 0xFE00 && codepoint <= 0xFE0F) || codepoint == 0xFEFF ||
               (codepoint >= 0xE0000 && codepoint <= 0xE0FFF);
    }
} // namespace Fenrir::Detail
