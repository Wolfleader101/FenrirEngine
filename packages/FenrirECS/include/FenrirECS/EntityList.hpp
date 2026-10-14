#pragma once

#include <entt/entity/registry.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include "FenrirECS/ComponentType.hpp"
#include "FenrirECS/DynamicComponent.hpp"

namespace Fenrir
{
    class Entity;
    class EntityList;

    /**
     * @brief EntityList which serves as a container for all entities and components
     *
     */
    class EntityList
    {
      public:
        /**
         * @brief Construct a new Entity List object
         *
         */
        EntityList() = default;

        /**
         * @brief Create a Entity object
         *
         * @return Entity that was created
         */
        Entity CreateEntity();

        /**
         * @brief Makes an entity id that is alive but has no components yet, not even the ones every entity gets. Code
         * that must hand an id out before it can change the list (deferred changes) uses this, then CompleteEntity when
         * it does.
         *
         * @return the new entity's id
         */
        [[nodiscard]] std::uint32_t ReserveEntity();

        /**
         * @brief Gives a reserved entity the components every entity has (CreateEntity does this for you). False if the
         * entity is not alive. Do not call it for an entity CreateEntity made: it would reset its Transform, Name and
         * Relationship.
         */
        bool CompleteEntity(std::uint32_t id);

        /**
         * @brief Get the Entity object
         *
         * @param id The id of the entity to get
         * @return Entity The entity that was retrieved
         */
        Entity GetEntity(uint32_t id);

        /**
         * @brief Destroy the Entity object
         *
         * @param id The id of the entity to destroy
         */
        void DestroyEntity(uint32_t id);

        /**
         * @brief Destroy the Entity object
         *
         * @param entity to destroy
         */
        void DestroyEntity(Entity entity);

        /**
         * @brief Check if the entity list has an entity with the given id
         *
         * @param id The id of the entity to check
         * @return true if the entity exists
         * @return false if the entity doesnt exist
         */
        bool HasEntity(uint32_t id);

        /**
         * @brief Clear the entity list
         *
         */
        void Clear();

        /**
         * @brief ForEach loop for all entities with the given components
         *
         * @tparam Components to loop over
         * @tparam Func to call on each entity
         * @param func to call on each entity
         */
        template <typename... Components, typename Func>
        void ForEach(Func&& func);

        /**
         * @brief ForEach loop for all entities
         *
         * @tparam Func to call on each entity
         * @param func to call on each entity
         */
        template <typename Func>
        void ForEachEntity(Func&& func);

        /**
         * @brief Group ForEach loop for all entities with the given components
         *
         * @tparam Components to loop over
         * @tparam Func to call on each entity
         * @param func to call on each entity
         */
        template <typename... Components, typename Func>
        void GroupForEach(Func&& func);

        /**
         * @brief Get the View object
         *
         * @tparam Components to get
         * @return auto the view
         */
        template <typename... Components>
        auto View();

        /**
         * @brief Get the Group object
         *
         * @tparam Components to get
         * @return auto the group
         */
        template <typename... Components>
        auto Group();

        /**
         * @name Components by their ComponentType
         * The same operations for every kind of component, native or dynamic, for code that knows a component only at
         * run time. A dynamic type's storage is created in this list when the first one is added; asking about one
         * nobody has added creates nothing. A native type's (empty) storage may be created by any of these, as asking
         * the ECS for a view of it would.
         * @{
         */

        /**
         * @brief The entity's component of this type, or null if the entity is not alive or has none. Never creates
         * the entity or gives it components. A dynamic component's pointer is good until a component of that type is
         * removed or this list is cleared; adding more does not move it.
         */
        [[nodiscard]] void* Get(const ComponentType& type, std::uint32_t entity);

        /** @brief Whether the entity is alive and has a component of this type. */
        [[nodiscard]] bool Has(const ComponentType& type, std::uint32_t entity);

        /**
         * @brief Gives the entity a default component of this type (default-constructed if native, zeroed if dynamic),
         * or keeps the one it has. False if the entity is not alive, a dynamic type is not valid, or this list already
         * holds components of that dynamic name with a different size or alignment.
         */
        bool Add(const ComponentType& type, std::uint32_t entity);

        /** @brief Removes the entity's component of this type, if it has one. False only if the entity is not alive. */
        bool Remove(const ComponentType& type, std::uint32_t entity);

        /** @brief How many entities have a component of this type. Constant time. */
        [[nodiscard]] std::size_t Count(const ComponentType& type);

        /**
         * @brief Removes every component of this type from every entity, and returns how many there were. The entities
         * stay. Afterwards a dynamic type's name may be added again with a different size or alignment.
         */
        std::size_t Clear(const ComponentType& type);

        /**
         * @brief Calls `func(entity)` for every entity that has a component of every listed type, in no particular
         * order. The types may mix native and dynamic. Do not add or remove components of the listed types, or destroy
         * entities, inside `func`. With no types nothing is visited.
         *
         * @param types the components an entity must have
         * @param func a callable taking the entity's id (std::uint32_t)
         */
        template <typename Func>
        void ForEachWith(std::span<const ComponentType> types, Func&& func);
        /** @} */

        /** @brief How many different kinds of component this list is holding storage for. A diagnostic, mostly. */
        [[nodiscard]] std::size_t ComponentStorageCount() const;

      private:
        entt::registry m_registry;

        void ForEachWithImpl(std::span<const ComponentType> types, void (*visit)(void* context, std::uint32_t entity),
                             void* context);

        // The storage the type's components are in. A native type's is created if this list has none; a dynamic type's
        // is null if none has been added. FindStorage and StorageForAdding are the only places that tell the kinds
        // apart: everything else works on the storage.
        entt::sparse_set* FindStorage(const ComponentType& type);

        // As FindStorage, but a dynamic type's storage is created and shaped for it. Null if the type is not valid, or
        // its name or id is taken by a storage of another shape or owner.
        entt::sparse_set* StorageForAdding(const ComponentType& type);

        // The storage of the native component type `T`, as ComponentType::Native<T> hands it out.
        template <typename T>
        static void* FindNativeStorage(EntityList& entities)
        {
            return static_cast<entt::sparse_set*>(&entities.m_registry.storage<T>());
        }

        friend class Entity;
        friend class ComponentType;
    };

    template <typename T>
    ComponentType ComponentType::Native()
    {
        static_assert(std::is_default_constructible_v<T>, "A component type must be default constructible.");
        static_assert(!std::is_empty_v<T>, "A component type reached by pointer must hold data.");
        return ComponentType(&EntityList::FindNativeStorage<T>, nullptr, sizeof(T), alignof(T));
    }

    template <typename Func>
    void EntityList::ForEachWith(std::span<const ComponentType> types, Func&& func)
    {
        ForEachWithImpl(
            types,
            [](void* context, std::uint32_t entity) {
                (*static_cast<std::remove_reference_t<Func>*>(context))(entity);
            },
            const_cast<std::remove_const_t<std::remove_reference_t<Func>>*>(std::addressof(func)));
    }

    template <typename... Components, typename Func>
    void EntityList::ForEach(Func&& func)
    {
        m_registry.view<Components...>().each(std::forward<Func>(func));
    }

    template <typename Func>
    void EntityList::ForEachEntity(Func&& func)
    {
        m_registry.view<entt::entity>().each(std::forward<Func>(func));
    }

    template <typename... Components, typename Func>
    void EntityList::GroupForEach(Func&& func)
    {
        m_registry.group<Components...>().each(std::forward<Func>(func));
    }

    template <typename... Components>
    auto EntityList::View()
    {
        return m_registry.view<Components...>();
    }

    template <typename... Components>
    auto EntityList::Group()
    {
        return m_registry.group<Components...>();
    }
} // namespace Fenrir