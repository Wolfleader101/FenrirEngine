#pragma once

#include <string>

#include "FenrirECS/DynamicComponent.hpp"

namespace Fenrir::Detail
{
    // What a script assembly reports about one component type it defines.
    struct ScriptComponentDescription
    {
        std::string name; // The type's full name.
        std::size_t size = 0;
        std::size_t alignment = 0;
    };

    // The ECS's description of the component type a script reported.
    [[nodiscard]] inline DynamicComponentType ToDynamicType(const ScriptComponentDescription& description)
    {
        return DynamicComponentType(description.name, description.size, description.alignment);
    }
} // namespace Fenrir::Detail
