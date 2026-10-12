#include "ScriptComponents.hpp"

#include <memory>

namespace Fenrir::Detail
{
    ComponentRegistration MakeScriptComponent(const ScriptComponentDescription& description)
    {
        // Shared by the operations below: one copy of the type's description.
        const auto type = std::make_shared<const DynamicComponentType>(ToDynamicType(description));

        ComponentRegistration registration;
        registration.name = description.name;
        registration.typeName = description.name;
        registration.scriptDefined = true;
        registration.size = description.size;
        registration.alignment = description.alignment;
        registration.get = [type](EntityList& entities, std::uint32_t entity) { return entities.GetDynamic(*type, entity); };
        registration.has = [type](EntityList& entities, std::uint32_t entity) { return entities.HasDynamic(*type, entity); };
        registration.add = [type](EntityList& entities, std::uint32_t entity) { return entities.AddDynamic(*type, entity); };
        registration.remove = [type](EntityList& entities, std::uint32_t entity) { return entities.RemoveDynamic(*type, entity); };
        registration.selector = ComponentSelector::Dynamic(*type);
        registration.count = [type](EntityList& entities) { return entities.CountDynamic(*type); };
        return registration;
    }
} // namespace Fenrir::Detail
