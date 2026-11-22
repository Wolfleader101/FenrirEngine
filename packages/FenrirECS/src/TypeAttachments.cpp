#include "FenrirECS/TypeAttachments.hpp"

#include "FenrirTypes/Kind.hpp"

namespace Fenrir
{
    std::optional<ComponentType> ComponentType::ForName(const TypeRegistry& registry, std::string_view name)
    {
        const ComponentType* type = registry.Attachment<ComponentTypeAttachment>(name);
        if (type == nullptr)
            return std::nullopt;
        return *type;
    }

    std::optional<std::string_view> ComponentType::NameOf(const TypeRegistry& registry, const ComponentType& type)
    {
        // Only a registered component carries the attachment, so the component role holds every candidate.
        for (const TypeDescription* component : registry.Types(Role::Component))
        {
            const ComponentType* attached = registry.Attachment<ComponentTypeAttachment>(component->name);
            if (attached != nullptr && attached->IsSameAs(type))
                return component->name;
        }
        return std::nullopt;
    }
} // namespace Fenrir
