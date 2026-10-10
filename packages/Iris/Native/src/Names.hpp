#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace Iris::Detail
{
    // Why a type or method name cannot be sent to the bridge, or nothing if it can. The bridge repeats
    // the null and UTF-8 checks; this keeps obviously bad names from crossing at all.
    inline std::optional<std::string> NameProblem(std::string_view name, const char* what)
    {
        if (name.empty() || name.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        {
            return std::string("A ") + what + " name must contain 1..INT32_MAX UTF-8 bytes.";
        }
        if (name.find(char{}) != std::string_view::npos)
        {
            return std::string("A ") + what + " name cannot contain null characters.";
        }
        return std::nullopt;
    }
} // namespace Iris::Detail
