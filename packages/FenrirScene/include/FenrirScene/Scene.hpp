#pragma once

// FenrirScene's role hook (FenrirTypes/RoleHooks.hpp): a generated registration lists this header in its
// `[registration] includes`, so the hook it calls for each `scene-resource` type is AttachSceneResource below, not the
// no-op its generated header would otherwise define.
#define FENRIR_TYPES_ATTACH_SCENE_RESOURCE_DEFINED

#include <memory>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <utility>

#include "FenrirECS/EntityList.hpp"
#include "FenrirTypes/RoleHooks.hpp"
#include "FenrirTypes/TypeDescription.hpp"
#include "FenrirTypes/TypeRegistry.hpp"

namespace Fenrir
{
    class Scene;

    /**
     * @brief A registered scene resource type's doors over a scene, for code that knows the type only by name:
     * `find` is `GetResource<T>()`, `add` a default-constructed `AddResource<T>()` and `remove` `RemoveResource<T>()`,
     * with the resource as an untyped pointer.
     */
    struct SceneResourceDoors
    {
        void* (*find)(Scene& scene) = nullptr;
        void* (*add)(Scene& scene) = nullptr;
        void (*remove)(Scene& scene) = nullptr;
    };

    /**
     * @brief The attachment tag FenrirScene hangs a registered scene resource type's doors under (ADR-0074), which
     * Scene's named FindResource, AddResource and RemoveResource open.
     */
    struct SceneResourceAttachment
    {
        using Value = SceneResourceDoors;
    };

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

        /**
         * @brief The scene's resource of the scene resource type registered under `name` (canonical or former), as
         * GetResource of that type gives it; null when the scene has none, or when `name` is no registered scene
         * resource type's. Reports nothing.
         */
        [[nodiscard]] void* FindResource(const TypeRegistry& registry, std::string_view name)
        {
            const SceneResourceDoors* doors = registry.Attachment<SceneResourceAttachment>(name);
            return doors != nullptr ? doors->find(*this) : nullptr;
        }

        /**
         * @brief Default-constructs the scene's resource of the scene resource type registered under `name`, as
         * AddResource of that type does, and gives it; null, adding nothing, when `name` is no registered scene
         * resource type's. Reports nothing.
         */
        void* AddResource(const TypeRegistry& registry, std::string_view name)
        {
            const SceneResourceDoors* doors = registry.Attachment<SceneResourceAttachment>(name);
            return doors != nullptr ? doors->add(*this) : nullptr;
        }

        /**
         * @brief Destroys the scene's resource of the scene resource type registered under `name`, as RemoveResource
         * of that type does. Does nothing when the scene has none, or when `name` is no registered scene resource
         * type's. Reports nothing.
         */
        void RemoveResource(const TypeRegistry& registry, std::string_view name)
        {
            const SceneResourceDoors* doors = registry.Attachment<SceneResourceAttachment>(name);
            if (doors != nullptr)
                doors->remove(*this);
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

    /**
     * @brief Hangs the scene resource type `T`'s doors (SceneResourceDoors) on the registered type `description`
     * describes, so Scene's named doors reach `T` by its name. `T` must be default constructible.
     *
     * @return Whether they were hung; the registry reports a refusal (no type registered under the description's name).
     */
    template <typename T>
    bool AttachSceneResource(TypeRegistry& registry, const TypeDescription& description)
    {
        return registry.Attach<SceneResourceAttachment>(
            description.name, SceneResourceDoors{
                                  .find = [](Scene& scene) -> void* { return scene.GetResource<T>(); },
                                  .add = [](Scene& scene) -> void* { return &scene.AddResource<T>(); },
                                  .remove = [](Scene& scene) { scene.RemoveResource<T>(); },
                              });
    }
} // namespace Fenrir
