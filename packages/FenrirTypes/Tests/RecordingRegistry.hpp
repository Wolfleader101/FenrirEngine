#pragma once

// What the registry tests read a registry's refusals through.
#include "FenrirTypes/TypeRegistry.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace TypesTest
{
    /** @brief A registry whose diagnostics land in a list the test reads. */
    struct RecordingRegistry
    {
        std::vector<std::string> diagnostics;
        Fenrir::TypeRegistry registry{[this](std::string_view message) { diagnostics.emplace_back(message); }};
    };

    /** @brief Whether `text` holds `part`. */
    inline bool Contains(const std::string& text, std::string_view part)
    {
        return text.find(part) != std::string::npos;
    }
} // namespace TypesTest
