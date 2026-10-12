#pragma once

#include <string>

#include "FenrirScripting/ComponentRegistry.hpp"

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

    // A registration whose operations act on the ECS's dynamic components of that type. The ECS creates storage in a scene
    // the first time one is added there, so registering this, and then discarding it, leaves no trace in any scene.
    [[nodiscard]] ComponentRegistration MakeScriptComponent(const ScriptComponentDescription& description);
} // namespace Fenrir::Detail
