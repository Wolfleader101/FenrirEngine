#pragma once

#include <entt/entity/registry.hpp>

#include <vector>

#include "EntityList.hpp"

namespace Fenrir
{
    /**
     * @brief Entity class that is used to interact with the entity list. This forms the basis of the ECS
     *
     */
    class Entity
    {
      public:
        /**
         * @brief Construct a new Entity object
         *
         */
        Entity() = default;

        /**
         * @brief Construct a new Entity object
         *
         * @param id The id of the entity
         * @param entityList The entity list that the entity belongs to
         */
        Entity(uint32_t id, EntityList* entityList);

        /**
         * @brief Add a component to the entity. If the component already exists it wont add it
         *
         * @tparam T The type of component to add
         * @tparam Args The arguments to pass to the constructor of the component
         * @param args The arguments to pass to the constructor of the component
         * @return T& The component that was added
         */
        template <typename T, typename... Args>
        T& AddComponent(Args&&... args);

        /**
         * @brief Remove a component from the entity, if it doesnt exist then it ignores
         *
         * @tparam T The type of component to remove
         */
        template <typename T>
        void RemoveComponent() const;

        /**
         * @brief Get the Component object
         *
         * @tparam T The type of component to get
         * @return T& The component that was retrieved
         */
        template <typename T>
        T& GetComponent() const;

        /**
         * @brief Check if the entity has a component
         *
         * @tparam T The type of component to check for
         * @return true if the entity has the component
         * @return false if the entity does not have the component
         */
        template <typename T>
        bool HasComponent() const;

        /**
         * @brief Check if the entity has any of the components
         *
         * @tparam T The type of components to check for
         * @return true if the entity has any of the components
         * @return false if the entity does not have any of the components
         */
        template <typename... T>
        bool HasAnyComponent() const;

        /**
         * @brief Get the Id object
         *
         * @return uint32_t The id of the entity
         */
        uint32_t GetId() const;

        /**
         * @brief Check if the entity is valid
         *
         * @return true if the entity is valid
         * @return false if the entity is not valid
         */
        bool IsValid() const;

      private:
        entt::entity m_entityId = entt::null;

        EntityList* m_entityList = nullptr;

        friend class EntityList;
    };

    template <typename T, typename... Args>
    T& Entity::AddComponent(Args&&... args)
    {
        if (HasComponent<T>())
        {
            //!! for now lets override but this might be bad??
            RemoveComponent<T>();

            // return GetComponent<T>();
        }

        return m_entityList->m_registry.emplace<T>(m_entityId, std::forward<Args>(args)...);
    }

    template <typename T>
    void Entity::RemoveComponent() const
    {
        if (!HasComponent<T>())
        {
            return;
        }

        m_entityList->m_registry.remove<T>(m_entityId);
    }

    template <typename T>
    T& Entity::GetComponent() const
    {
        // TODO add error handling
        return m_entityList->m_registry.get<T>(m_entityId);
    }

    template <typename T>
    bool Entity::HasComponent() const
    {
        return m_entityList->m_registry.all_of<T>(m_entityId);
    }

    template <typename... T>
    bool Entity::HasAnyComponent() const
    {
        return m_entityList->m_registry.any_of<T...>(m_entityId);
    }
} // namespace Fenrir