#include "FenrirECS/EntityList.hpp"

#include "FenrirECS/Entity.hpp"

#include "ByteStorage.hpp"

#include <entt/entity/runtime_view.hpp>

namespace Fenrir
{
    Entity EntityList::CreateEntity()
    {
        Entity entity(static_cast<uint32_t>(m_registry.create()), this);

        return entity;
    }


    std::uint32_t EntityList::ReserveEntity()
    {
        return static_cast<std::uint32_t>(m_registry.create());
    }

    bool EntityList::CompleteEntity(std::uint32_t id)
    {
        if (!m_registry.valid(static_cast<entt::entity>(id)))
        {
            return false;
        }
        const Entity completed(id, this); // The creating constructor: Transform, Name and Relationship.
        (void)completed;
        return true;
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

    namespace
    {
        // Whatever the registry holds under a dynamic type id, if it is one of our pools. An id is a hash, so it can in principle
        // belong to a storage of something else; the typed lookup trusts the id, so this checks first.
        Detail::ByteStorage* PoolUnder(entt::registry& registry, const DynamicComponentType& type)
        {
            entt::sparse_set* storage = registry.storage(type.Id());
            return storage != nullptr && storage->type() == entt::type_id<Detail::DynamicComponentTag>()
                       ? static_cast<Detail::ByteStorage*>(storage)
                       : nullptr;
        }

        // The list's pool for a dynamic type if it has one. Reading never creates a pool. A pool under the id that belongs to a
        // different type name (the same hash) is not this type's.
        Detail::ByteStorage* ExistingPool(entt::registry& registry, const DynamicComponentType& type)
        {
            Detail::ByteStorage* pool = PoolUnder(registry, type);
            return pool != nullptr && pool->Owner() == type.Name() ? pool : nullptr;
        }

        // The pool, created and shaped for the type if the list has none yet. Null if the type is not valid, if it exists with a
        // different shape, or if the id is taken by something else (another name with the same hash, or a native storage).
        Detail::ByteStorage* Pool(entt::registry& registry, const DynamicComponentType& type)
        {
            if (!type.IsValid())
            {
                return nullptr;
            }
            if (registry.storage(type.Id()) != nullptr)
            {
                Detail::ByteStorage* existing = PoolUnder(registry, type);
                if (existing == nullptr || !existing->MayBeUsedBy(type.Name()))
                {
                    return nullptr;
                }
            }
            auto& storage = registry.storage<Detail::DynamicComponentTag>(type.Id());
            if (!storage.Configure(type.Size(), type.Alignment()))
            {
                return nullptr;
            }
            storage.Claim(type.Name());
            return &storage;
        }

        entt::entity AsEntity(std::uint32_t id)
        {
            return static_cast<entt::entity>(id);
        }
    } // namespace

    void* EntityList::GetDynamic(const DynamicComponentType& type, std::uint32_t entity)
    {
        if (!m_registry.valid(AsEntity(entity)))
        {
            return nullptr;
        }
        Detail::ByteStorage* pool = ExistingPool(m_registry, type);
        return pool == nullptr ? nullptr : pool->Find(AsEntity(entity));
    }

    bool EntityList::HasDynamic(const DynamicComponentType& type, std::uint32_t entity)
    {
        if (!m_registry.valid(AsEntity(entity)))
        {
            return false;
        }
        Detail::ByteStorage* pool = ExistingPool(m_registry, type);
        return pool != nullptr && pool->contains(AsEntity(entity));
    }

    bool EntityList::AddDynamic(const DynamicComponentType& type, std::uint32_t entity)
    {
        if (!m_registry.valid(AsEntity(entity)))
        {
            return false;
        }
        Detail::ByteStorage* pool = Pool(m_registry, type);
        return pool != nullptr && pool->Emplace(AsEntity(entity)) != nullptr;
    }

    bool EntityList::RemoveDynamic(const DynamicComponentType& type, std::uint32_t entity)
    {
        if (!m_registry.valid(AsEntity(entity)))
        {
            return false;
        }
        if (Detail::ByteStorage* pool = ExistingPool(m_registry, type))
        {
            pool->remove(AsEntity(entity));
        }
        return true;
    }

    std::size_t EntityList::CountDynamic(const DynamicComponentType& type)
    {
        const Detail::ByteStorage* pool = ExistingPool(m_registry, type);
        return pool == nullptr ? 0 : pool->size();
    }

    std::size_t EntityList::ComponentStorageCount() const
    {
        std::size_t count = 0;
        for (auto&& [id, storage] : m_registry.storage())
        {
            (void)id;
            (void)storage;
            ++count;
        }
        return count;
    }

    std::size_t EntityList::CountOf(const ComponentSelector& selector)
    {
        const auto* pool = static_cast<const entt::sparse_set*>(selector.m_find(*this));
        return pool == nullptr ? 0 : pool->size();
    }

    ComponentSelector ComponentSelector::Dynamic(DynamicComponentType type)
    {
        return ComponentSelector([type = std::move(type)](EntityList& entities) -> void* {
            // No pool yet means nobody has added one, so nothing can match: the query visits nothing, and makes no storage.
            return static_cast<entt::sparse_set*>(ExistingPool(entities.m_registry, type));
        });
    }

    void EntityList::ForEachWithImpl(std::span<const ComponentSelector> selectors, void (*visit)(void*, std::uint32_t), void* context)
    {
        if (selectors.empty())
        {
            return;
        }
        entt::runtime_view view;
        for (const ComponentSelector& selector : selectors)
        {
            auto* pool = static_cast<entt::sparse_set*>(selector.m_find(*this));
            if (pool == nullptr)
            {
                return; // A type with no usable storage: no entity has all of them.
            }
            view.iterate(*pool);
        }
        for (const entt::entity entity : view)
        {
            visit(context, static_cast<std::uint32_t>(entity));
        }
    }

} // namespace Fenrir