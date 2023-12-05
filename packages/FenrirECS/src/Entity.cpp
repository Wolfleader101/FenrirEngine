#include "FenrirECS/Entity.hpp"

#include "FenrirECS/DefaultComponents.hpp"

namespace Fenrir
{
    Entity::Entity(uint32_t id, EntityList* entityList)
        : m_entityId(static_cast<entt::entity>(id)), m_entityList(entityList)
    {
        AddComponent<Transform>();
        AddComponent<Name>();
    }

    uint32_t Entity::GetId() const
    {
        return static_cast<uint32_t>(m_entityId);
    }

    bool Entity::IsValid() const
    {
        return m_entityList->m_registry.valid(m_entityId);
    }
} // namespace Fenrir