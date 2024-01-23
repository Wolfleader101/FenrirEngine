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
         * @brief Compare two entities
         *
         * @param other The other entity to compare to
         * @return true if the entities are the same
         * @return false if the entities are not the same
         */
        bool operator==(const Entity& other) const;

        /**
         * @brief Compare two entities
         *
         * @param other The other entity to compare to
         * @return true if the entities are not the same
         * @return false if the entities are the same
         */
        bool operator!=(const Entity& other) const;

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

        /**
         * @brief Iterate over all the children of the entity and call the function on them
         *
         * @tparam Func The type of function to call
         * @param func The function to call
         */
        template <typename Func>
        void ForEachChild(Func&& func) const;

        /**
         * @brief Add a child to the entity
         *
         * @param child The child to add
         */
        void AddChild(Entity child) const;

        /**
         * @brief Remove a child from the entity
         *
         * @param child The child to remove
         */
        void RemoveChild(Entity child) const;

        static uint32_t Null;

      private:
        entt::entity m_entityId = entt::null;

        EntityList* m_entityList = nullptr;

        friend class EntityList;
    };

    // this needs to be here because it needs to know about entity
    struct Relationship
    {
        Relationship() = default;

        Entity parent = Entity();
        Entity firstChild = Entity();
        Entity nextSibling = Entity();
        Entity prevSibling = Entity();
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

    template <typename Func>
    void Entity::ForEachChild(Func&& func) const
    {
        Relationship& relationship = GetComponent<Relationship>();

        if (relationship.firstChild.IsValid())
        {
            auto child = relationship.firstChild;

            while (child.IsValid())
            {
                func(child);

                child = child.GetComponent<Relationship>().nextSibling;
            }
        }
    }
} // namespace Fenrir