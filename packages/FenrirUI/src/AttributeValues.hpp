#pragma once

#include <charconv>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string_view>
#include <system_error>

// Reading markup attribute values (Element::SetAttribute): the forms the controls share.
namespace Fenrir::Detail
{
    /** @brief A boolean attribute: `true`, `false`, or bare (empty) for true; nothing for anything else. */
    [[nodiscard]] inline std::optional<bool> ParseBoolAttribute(std::string_view value) noexcept
    {
        if (value.empty() || value == "true")
        {
            return true;
        }
        if (value == "false")
        {
            return false;
        }
        return std::nullopt;
    }

    /** @brief A finite decimal number, all of `value`; nothing for anything else. */
    [[nodiscard]] inline std::optional<float> ParseNumberAttribute(std::string_view value) noexcept
    {
        float number = 0.0f;
        const char* end = value.data() + value.size();
        const auto [at, error] = std::from_chars(value.data(), end, number);
        if (value.empty() || error != std::errc() || at != end || !std::isfinite(number))
        {
            return std::nullopt;
        }
        return number;
    }

    /** @brief A count, a whole number from 0, all of `value`; nothing for anything else. */
    [[nodiscard]] inline std::optional<std::size_t> ParseCountAttribute(std::string_view value) noexcept
    {
        std::size_t count = 0;
        const char* end = value.data() + value.size();
        const auto [at, error] = std::from_chars(value.data(), end, count);
        if (value.empty() || error != std::errc() || at != end)
        {
            return std::nullopt;
        }
        return count;
    }
} // namespace Fenrir::Detail
