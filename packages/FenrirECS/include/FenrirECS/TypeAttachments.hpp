#pragma once

// FenrirECS's role hook (FenrirTypes/RoleHooks.hpp): a generated registration lists this header in its
// `[registration] includes`, so the hook it calls for each `component` type is this one, not the no-op its generated
// header would otherwise define.
#define FENRIR_TYPES_ATTACH_COMPONENT_DEFINED

#include "FenrirECS/ComponentType.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirTypes/RoleHooks.hpp"
#include "FenrirTypes/TypeDescription.hpp"
#include "FenrirTypes/TypeRegistry.hpp"

namespace Fenrir
{
    /**
     * @brief The attachment tag FenrirECS hangs a registered component's ComponentType under (ADR-0074), which
     * ComponentType::ForName and NameOf read.
     */
    struct ComponentTypeAttachment
    {
        using Value = ComponentType;
    };

    /**
     * @brief Hangs `ComponentType::Native<T>()` on the registered component type `description` describes, so code
     * holding only its name reaches its components through EntityList.
     *
     * @return Whether it was hung; the registry reports a refusal (no type registered under the description's name).
     */
    template <typename T>
    bool AttachComponent(TypeRegistry& registry, const TypeDescription& description)
    {
        return registry.Attach<ComponentTypeAttachment>(description.name, ComponentType::Native<T>());
    }
} // namespace Fenrir
