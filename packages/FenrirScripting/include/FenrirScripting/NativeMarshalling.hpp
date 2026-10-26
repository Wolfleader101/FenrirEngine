#pragma once

// How a native function reads and writes the wire shapes a generated declaration cannot type: a span C# passes as its
// first element and a count, text C# receives into a buffer of a given capacity, and UTF-8 as C#'s UTF-16. Wire shape,
// not call checks: those are NativeGuard's.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Fenrir
{
    /**
     * @brief The span C# passed as its first element `first` and its length `count`: empty for a null `first` (which
     * is how an empty span arrives; ADR-0045) or a count that is not positive. The one place the bounds are taken on
     * C#'s word.
     */
    template <typename T>
    [[nodiscard]] std::span<T> SpanOf(T* first, std::int32_t count) noexcept
    {
        if (first == nullptr || count <= 0)
        {
            return {};
        }
        // C# passes a span as its first element and its length, so the bounds are only as good as C#'s.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
#endif
        return {first, static_cast<std::size_t>(count)};
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    /**
     * @brief Writes as much of `text` as `capacity` chars from `out` hold (nothing for a null `out` or a capacity that
     * is not positive) and returns the whole text's length, so C# can tell a cut-short copy and ask again with room.
     */
    [[nodiscard]] inline std::int32_t WriteText(char* out, std::int32_t capacity, std::string_view text) noexcept
    {
        const std::span<char> room = SpanOf(out, capacity);
        std::copy_n(text.begin(), std::min(room.size(), text.size()), room.begin());
        return static_cast<std::int32_t>(text.size());
    }

    /** @brief WriteText for C#'s chars: UTF-16 `text` into a buffer of `capacity` chars. */
    [[nodiscard]] inline std::int32_t WriteText(char16_t* out, std::int32_t capacity, std::u16string_view text) noexcept
    {
        const std::span<char16_t> room = SpanOf(out, capacity);
        std::copy_n(text.begin(), std::min(room.size(), text.size()), room.begin());
        return static_cast<std::int32_t>(text.size());
    }

    /**
     * @brief `text`, UTF-8, as UTF-16 (C#'s chars). A byte that does not start a valid UTF-8 sequence (a stray
     * continuation byte, a sequence cut short, an overlong form, a surrogate or a value past U+10FFFF) becomes U+FFFD,
     * and decoding goes on from the next byte.
     */
    [[nodiscard]] inline std::u16string Utf8ToUtf16(std::string_view text)
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
} // namespace Fenrir
