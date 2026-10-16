#pragma once

#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>

#include "FenrirECS/EntityList.hpp"

namespace Fenrir
{
    class Scene
    {
      public:
        Scene(const std::string& name);

        const std::string& GetName() const;

        void SetName(const std::string& name);

        const EntityList& GetEntityList() const;

        EntityList& GetEntityList();

        /**
         * @brief The scene's resource of type `T` (state a package keeps in this scene, one value per type), or null if
         * it has none. The pointer stays valid until the resource is replaced or removed, or the scene ends.
         */
        template <typename T>
        [[nodiscard]] T* GetResource()
        {
            const auto it = m_resources.find(std::type_index(typeid(T)));
            return it == m_resources.end() ? nullptr : static_cast<T*>(it->second.get());
        }

        /** @copydoc GetResource */
        template <typename T>
        [[nodiscard]] const T* GetResource() const
        {
            const auto it = m_resources.find(std::type_index(typeid(T)));
            return it == m_resources.end() ? nullptr : static_cast<const T*>(it->second.get());
        }

        /**
         * @brief Constructs the scene's resource of type `T` from `args`, replacing (and destroying) any it already
         * had. The new resource is constructed before the old one is destroyed. It is destroyed when the scene ends.
         */
        template <typename T, typename... Args>
        T& AddResource(Args&&... args)
        {
            ResourcePtr resource(new T(std::forward<Args>(args)...), &Destroy<T>);
            T& added = *static_cast<T*>(resource.get());
            m_resources.insert_or_assign(std::type_index(typeid(T)), std::move(resource));
            return added;
        }

        /** @brief Destroys the scene's resource of type `T`. Does nothing if the scene has none. */
        template <typename T>
        void RemoveResource()
        {
            m_resources.erase(std::type_index(typeid(T)));
        }

      private:
        // The deleter is chosen where the type is known, so the scene stores resources of any type without knowing it.
        using ResourcePtr = std::unique_ptr<void, void (*)(void*)>;

        template <typename T>
        static void Destroy(void* resource)
        {
            delete static_cast<T*>(resource);
        }

        std::string m_name;

        EntityList m_entityList;

        // Declared after the entity list so resources, which may refer to the scene's entities, end first.
        std::unordered_map<std::type_index, ResourcePtr> m_resources;
    };
} // namespace Fenrir
