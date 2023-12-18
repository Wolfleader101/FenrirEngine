#pragma once

#include <entt/entity/registry.hpp>

#include <vector>

namespace Fenrir
{
    class Entity;

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

      private:
        entt::registry m_registry;

        friend class Entity;
    };

    template <typename... Components, typename Func>
    void EntityList::ForEach(Func&& func)
    {
        m_registry.view<Components...>().each(std::forward<Func>(func));
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