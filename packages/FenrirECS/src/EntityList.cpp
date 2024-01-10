#include "FenrirECS/EntityList.hpp"

#include "FenrirECS/Entity.hpp"

namespace Fenrir
{
    Entity EntityList::CreateEntity()
    {
        Entity entity(static_cast<uint32_t>(m_registry.create()), this);

        return entity;
    }

    Entity EntityList::GetEntity(uint32_t id)
    {
        if (m_registry.valid(static_cast<entt::entity>(id)))
        {
            // cannot use the constructor as it will create new components for the entity
            // return Entity(id, this);

            Entity newEntity;
            newEntity.m_entityId = static_cast<entt::entity>(id);
            newEntity.m_entityList = this;

            return newEntity;
        }

        return Entity();
    }

    void EntityList::DestroyEntity(uint32_t id)
    {
        if (m_registry.valid(static_cast<entt::entity>(id)))
        {
            m_registry.destroy(static_cast<entt::entity>(id));
        }
    }

    void EntityList::DestroyEntity(Entity entity)
    {
        if (m_registry.valid(static_cast<entt::entity>(entity.GetId())))
        {
            m_registry.destroy(static_cast<entt::entity>(entity.GetId()));
        }
    }

    bool EntityList::HasEntity(uint32_t id)
    {
        return m_registry.valid(static_cast<entt::entity>(id));
    }

    void EntityList::Clear()
    {
        m_registry.clear();
    }

} // namespace Fenrir