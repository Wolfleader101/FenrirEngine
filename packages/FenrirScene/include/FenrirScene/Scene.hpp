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

        // PROTOTYPE (physics spike): scene resources, one value per type, ended with the scene.
        template <typename T>
        T* GetResource()
        {
            auto it = m_resources.find(typeid(T));
            return it == m_resources.end() ? nullptr : static_cast<T*>(it->second.get());
        }

        template <typename T, typename... Args>
        T& AddResource(Args&&... args)
        {
            auto resource = std::make_shared<T>(std::forward<Args>(args)...);
            T& ref = *resource;
            m_resources[typeid(T)] = std::move(resource);
            return ref;
        }

        template <typename T>
        void RemoveResource()
        {
            m_resources.erase(typeid(T));
        }

      private:
        std::string m_name;

        EntityList m_entityList;

        // PROTOTYPE: shared_ptr<void> keeps the deleter, so the scene needs no knowledge of the types.
        std::unordered_map<std::type_index, std::shared_ptr<void>> m_resources;
    };
} // namespace Fenrir
