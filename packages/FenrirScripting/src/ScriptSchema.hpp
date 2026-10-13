#pragma once

#include <cstddef>
#include <map>
#include <string>

#include <Iris/Iris.hpp>

#include "ComponentRegistry.hpp"
#include "EngineContext.hpp"

namespace Fenrir::Detail
{
    // What a script component or event looked like when its data was last in use, so a reload can tell whether the data
    // still means the same thing: the size and alignment, and each field's name, type, kind and offset.
    struct OwnedLayout
    {
        std::string fingerprint;
        std::size_t size = 0;
        std::size_t alignment = 0;
    };
    using OwnedLayouts = std::map<std::string, OwnedLayout>;

    // Checks one native component against its C# struct in a loaded script assembly. The diagnostic names the component
    // and the first mismatching field.
    [[nodiscard]] Iris::CallResult<void> ValidateRegistration(const ComponentRegistration& registration,
                                                              const Iris::Component& component);

    // Everything that must hold before a freshly loaded script assembly is kept, and then the keeping. It either
    // commits the whole load or changes nothing: what the assembly defines is discovered and checked, every native
    // component and event is checked against its C# struct, and only then is what the previous generation left in the
    // ECS and the App compared with this one's definitions (unchanged data stays, anything else goes with a warning)
    // and the new definitions registered. `ownedComponents` and `ownedEvents` are what the ECS and the App still hold.
    [[nodiscard]] Iris::CallResult<void> CompleteLoad(EngineContext& context, ComponentRegistry& registry,
                                                      const Iris::Component& component, OwnedLayouts& ownedComponents,
                                                      OwnedLayouts& ownedEvents);
} // namespace Fenrir::Detail
