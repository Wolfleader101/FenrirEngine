#pragma once

#include <entt/entity/registry.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include "FenrirECS/DynamicComponent.hpp"

namespace Fenrir
{
    class Entity;
    class EntityList;

    /**
     * @brief Names one kind of component, native or dynamic, for a query: "entities that have this". A query takes any
     * mix.
     *
     * It says nothing about how the ECS stores components, so code that builds queries from selectors keeps working if
     * the ECS changes underneath. Cheap to copy; make them once, not per frame.
     */
    class ComponentSelector
    {
      public:
        /** @brief A C++ component type. */
        template <typename T>
        [[nodiscard]] static ComponentSelector Native();

        /** @brief A component type known only at run time. */
        [[nodiscard]] static ComponentSelector Dynamic(DynamicComponentType type);

      private:
        friend class EntityList;
        using Finder = std::function<void*(EntityList&)>;
        explicit ComponentSelector(Finder find) : m_find(std::move(find))
        {
        }
        Finder m_find;
    };

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
         * @name Components known only at run time
         * Components of a DynamicComponentType: plain bytes of a given size and alignment, one per entity, zeroed when
         * added and removed with the entity. A scripting layer uses these for components that scripts define. The
         * storage for a type is created in this list when the first one is added; asking about a type nobody has added
         * creates nothing.
         * @{
         */

        /**
         * @brief The entity's component of this type, or null if the entity is not alive or has none. The pointer is
         * good until a component of this type is removed or this list is cleared.
         */
        [[nodiscard]] void* GetDynamic(const DynamicComponentType& type, std::uint32_t entity);

        /** @brief Whether the entity is alive and has a component of this type. */
        [[nodiscard]] bool HasDynamic(const DynamicComponentType& type, std::uint32_t entity);

        /**
         * @brief Gives the entity a zeroed component of this type (or keeps the one it has). False if the entity is not
         * alive, the type is not valid, or this list already holds components of that name with a different size or
         * alignment.
         */
        bool AddDynamic(const DynamicComponentType& type, std::uint32_t entity);

        /** @brief Removes the entity's component of this type, if any. False if the entity is not alive. */
        bool RemoveDynamic(const DynamicComponentType& type, std::uint32_t entity);

        /** @brief How many entities have a component of this type. */
        [[nodiscard]] std::size_t CountDynamic(const DynamicComponentType& type);

        /**
         * @brief Removes every component of this type from every entity, and returns how many there were. Afterwards
         * the type may be added again with a different size or alignment. Do nothing if no entity has one.
         */
        std::size_t ClearDynamic(const DynamicComponentType& type);
        /** @} */

        /**
         * @brief Calls `func(entity)` for every entity that has all of the selected components, in no particular order.
         * The selectors may mix native and dynamic components. Do not add or remove components of the selected kinds,
         * or destroy entities, inside `func`. With no selectors nothing is visited.
         *
         * @param selectors the components an entity must have
         * @param func a callable taking the entity's id (std::uint32_t)
         */
        template <typename Func>
        void ForEachWith(std::span<const ComponentSelector> selectors, Func&& func);

        /**
         * @brief How many entities have the selected component. Constant time. (Selecting a native component makes its
         * storage if the list has none, as asking the ECS for a view of it would; a dynamic one never does.)
         */
        [[nodiscard]] std::size_t CountOf(const ComponentSelector& selector);

        /** @brief How many different kinds of component this list is holding storage for. A diagnostic, mostly. */
        [[nodiscard]] std::size_t ComponentStorageCount() const;

      private:
        entt::registry m_registry;

        void ForEachWithImpl(std::span<const ComponentSelector> selectors,
                             void (*visit)(void* context, std::uint32_t entity), void* context);

        // The storage for a native component type, as the opaque pointer a ComponentSelector hands back.
        template <typename T>
        void* NativePool()
        {
            return static_cast<entt::sparse_set*>(&m_registry.storage<T>());
        }

        friend class Entity;
        friend class ComponentSelector;
    };

    template <typename T>
    ComponentSelector ComponentSelector::Native()
    {
        return ComponentSelector([](EntityList& entities) { return entities.NativePool<T>(); });
    }

    template <typename Func>
    void EntityList::ForEachWith(std::span<const ComponentSelector> selectors, Func&& func)
    {
        ForEachWithImpl(
            selectors,
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