#pragma once

#include <string>
#include <vector>

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

    // An event type a script assembly reported while it loaded: a name, a size and an alignment, as for a component.
    using ScriptEventDescription = ScriptComponentDescription;

    // The script report: what a script assembly says it defines when it loads, the C# components and events, each with
    // its size and alignment, in the order it reports them. The engine checks it before it keeps the assembly.
    struct ScriptReport
    {
        std::vector<ScriptComponentDescription> components;
        std::vector<ScriptEventDescription> events;
    };

    // The ECS's description of the component type a script reported.
    [[nodiscard]] inline DynamicComponentType ToDynamicType(const ScriptComponentDescription& description)
    {
        return DynamicComponentType(description.name, description.size, description.alignment);
    }
} // namespace Fenrir::Detail
