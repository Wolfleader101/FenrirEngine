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

    Entity EntityList::HandleOf(std::uint32_t id)
    {
        Entity handle;
        handle.m_entityId = static_cast<entt::entity>(id);
        handle.m_entityList = this;
        return handle;
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
        // Whatever the registry holds under a dynamic type id, if it is one of our byte storages. An id is a hash, so
        // it can in principle belong to a storage of something else; the typed lookup trusts the id, so this checks
        // first.
        Detail::ByteStorage* DynamicStorageUnder(entt::registry& registry, const DynamicComponentType& type)
        {
            entt::sparse_set* storage = registry.storage(type.Id());
            return storage != nullptr && storage->type() == entt::type_id<Detail::DynamicComponentTag>()
                       ? static_cast<Detail::ByteStorage*>(storage)
                       : nullptr;
        }

        // The list's storage for a dynamic type if it has one. Reading never creates one. A storage under the id that
        // belongs to a different type name (the same hash) is not this type's.
        Detail::ByteStorage* ExistingDynamicStorage(entt::registry& registry, const DynamicComponentType& type)
        {
            Detail::ByteStorage* storage = DynamicStorageUnder(registry, type);
            return storage != nullptr && storage->Owner() == type.Name() ? storage : nullptr;
        }

        // The storage, created and shaped for the type if the list has none yet. Null if the type is not valid, if it
        // exists with a different shape, or if the id is taken by something else (another name with the same hash, or a
        // native storage).
        Detail::ByteStorage* DynamicStorageForAdding(entt::registry& registry, const DynamicComponentType& type)
        {
            if (!type.IsValid())
            {
                return nullptr;
            }
            if (registry.storage(type.Id()) != nullptr)
            {
                Detail::ByteStorage* existing = DynamicStorageUnder(registry, type);
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

        // The storage a watch on a dynamic type listens to: the type's own, made and claimed for its name (but not yet
        // shaped) if nobody has added one, so that the watch hears the first add. Null if the type is not valid or its
        // id is taken by something else: then nothing of the type can ever be added here.
        Detail::ByteStorage* DynamicStorageForWatching(entt::registry& registry, const DynamicComponentType& type)
        {
            if (!type.IsValid())
            {
                return nullptr;
            }
            if (registry.storage(type.Id()) == nullptr)
            {
                (void)registry.storage<Detail::DynamicComponentTag>(type.Id());
            }
            Detail::ByteStorage* storage = DynamicStorageUnder(registry, type);
            if (storage == nullptr || !storage->MayBeUsedBy(type.Name()))
            {
                return nullptr;
            }
            storage->Claim(type.Name());
            return storage;
        }

        // A dynamic type's counterpart of ComponentType::NativeWatchHook: records straight from the byte storage.
        void HookDynamicWatch(void* storage, Detail::WatchRecord& record, bool start)
        {
            auto* bytes = static_cast<Detail::ByteStorage*>(static_cast<entt::sparse_set*>(storage));
            if (start)
            {
                bytes->StartRecording(record);
            }
            else
            {
                bytes->StopRecording(record);
            }
        }

        entt::entity AsEntity(std::uint32_t id)
        {
            return static_cast<entt::entity>(id);
        }
    } // namespace

    entt::sparse_set* EntityList::FindStorage(const ComponentType& type)
    {
        return type.IsDynamic() ? ExistingDynamicStorage(m_registry, *type.m_dynamic)
                                : static_cast<entt::sparse_set*>(type.m_findNativeStorage(*this));
    }

    entt::sparse_set* EntityList::StorageForAdding(const ComponentType& type)
    {
        return type.IsDynamic() ? DynamicStorageForAdding(m_registry, *type.m_dynamic)
                                : static_cast<entt::sparse_set*>(type.m_findNativeStorage(*this));
    }

    void* EntityList::Get(const ComponentType& type, std::uint32_t entity)
    {
        if (!m_registry.valid(AsEntity(entity)))
        {
            return nullptr;
        }
        entt::sparse_set* storage = FindStorage(type);
        return storage != nullptr && storage->contains(AsEntity(entity)) ? storage->value(AsEntity(entity)) : nullptr;
    }

    bool EntityList::Has(const ComponentType& type, std::uint32_t entity)
    {
        if (!m_registry.valid(AsEntity(entity)))
        {
            return false;
        }
        const entt::sparse_set* storage = FindStorage(type);
        return storage != nullptr && storage->contains(AsEntity(entity));
    }

    bool EntityList::Add(const ComponentType& type, std::uint32_t entity)
    {
        if (!m_registry.valid(AsEntity(entity)))
        {
            return false;
        }
        entt::sparse_set* storage = StorageForAdding(type);
        if (storage == nullptr)
        {
            return false;
        }
        // With no value, a storage default-constructs a native component and zeroes a dynamic one.
        return storage->contains(AsEntity(entity)) || storage->push(AsEntity(entity)) != storage->end();
    }

    bool EntityList::Remove(const ComponentType& type, std::uint32_t entity)
    {
        if (!m_registry.valid(AsEntity(entity)))
        {
            return false;
        }
        if (entt::sparse_set* storage = FindStorage(type))
        {
            storage->remove(AsEntity(entity));
        }
        return true;
    }

    std::size_t EntityList::Count(const ComponentType& type)
    {
        const entt::sparse_set* storage = FindStorage(type);
        return storage == nullptr ? 0 : storage->size();
    }

    std::size_t EntityList::Clear(const ComponentType& type)
    {
        entt::sparse_set* storage = FindStorage(type);
        if (storage == nullptr)
        {
            return 0;
        }
        const std::size_t removed = storage->size();
        storage->clear();
        return removed;
    }

    ComponentWatch EntityList::Watch(const ComponentType& type)
    {
        if (m_alive == nullptr)
        {
            m_alive = std::make_shared<const int>(0);
        }
        auto record = std::make_unique<Detail::WatchRecord>();
        record->list = m_alive;
        if (!type.IsDynamic())
        {
            record->storage = type.m_findNativeStorage(*this);
            record->hook = type.m_hookNativeWatch;
        }
        else if (Detail::ByteStorage* storage = DynamicStorageForWatching(m_registry, *type.m_dynamic))
        {
            record->storage = static_cast<entt::sparse_set*>(storage);
            record->hook = &HookDynamicWatch;
        }
        if (record->hook != nullptr)
        {
            record->hook(record->storage, *record, true);
        }
        return ComponentWatch(std::move(record));
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

    void EntityList::ForEachWithImpl(std::span<const ComponentType> types, void (*visit)(void*, std::uint32_t),
                                     void* context)
    {
        if (types.empty())
        {
            return;
        }
        entt::runtime_view view;
        for (const ComponentType& type : types)
        {
            // No storage for a dynamic type means nobody has added one, so nothing can match: the query visits
            // nothing, and makes no storage.
            entt::sparse_set* storage = FindStorage(type);
            if (storage == nullptr)
            {
                return;
            }
            view.iterate(*storage);
        }
        for (const entt::entity entity : view)
        {
            visit(context, static_cast<std::uint32_t>(entity));
        }
    }

} // namespace Fenrir
