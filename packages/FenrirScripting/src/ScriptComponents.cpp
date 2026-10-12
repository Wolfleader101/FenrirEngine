#include "ScriptComponents.hpp"

namespace Fenrir::Detail
{
    entt::id_type ScriptStorageId(std::string_view name)
    {
        const std::string prefixed = "fenrir.script:" + std::string(name);
        return entt::hashed_string::value(prefixed.c_str(), prefixed.size());
    }

    bool IsPlausible(const ScriptComponentDescription& description) noexcept
    {
        const std::size_t alignment = description.alignment;
        return !description.name.empty() && description.size > 0 && description.size <= ByteStorage::MaxElementSize &&
               alignment > 0 && (alignment & (alignment - 1)) == 0 && description.size % alignment == 0;
    }

    namespace
    {
        // The pool for a component in a list, created and configured with the component's size and alignment if the list has
        // none yet. Null if the storage already exists with a different configuration.
        ByteStorage* Pool(EntityList& entities, entt::id_type id, std::size_t size, std::size_t alignment)
        {
            auto& storage = entities.GetRegistry().storage<ScriptComponentTag>(id);
            return storage.Configure(size, alignment) ? &storage : nullptr;
        }

        // The pool if the list already has one. Asking whether an entity has a component, or for its data, never creates a
        // pool: a component nobody has added in a scene costs that scene nothing.
        ByteStorage* ExistingPool(EntityList& entities, entt::id_type id)
        {
            entt::sparse_set* storage = entities.GetRegistry().storage(id);
            return storage != nullptr && storage->type() == entt::type_id<ScriptComponentTag>() ? static_cast<ByteStorage*>(storage)
                                                                                                : nullptr;
        }
    } // namespace

    ComponentRegistration MakeScriptComponent(const ScriptComponentDescription& description)
    {
        const entt::id_type id = ScriptStorageId(description.name);
        const std::size_t size = description.size;
        const std::size_t alignment = description.alignment;

        ComponentRegistration registration;
        registration.name = description.name;
        registration.typeName = description.name;
        registration.scriptDefined = true;
        registration.size = size;
        registration.alignment = alignment;
        registration.get = [id](EntityList& entities, std::uint32_t entity) -> void* {
            if (!entities.GetRegistry().valid(static_cast<entt::entity>(entity)))
            {
                return nullptr;
            }
            ByteStorage* pool = ExistingPool(entities, id);
            return pool == nullptr ? nullptr : pool->Find(static_cast<entt::entity>(entity));
        };
        registration.has = [id](EntityList& entities, std::uint32_t entity) {
            if (!entities.GetRegistry().valid(static_cast<entt::entity>(entity)))
            {
                return false;
            }
            ByteStorage* pool = ExistingPool(entities, id);
            return pool != nullptr && pool->contains(static_cast<entt::entity>(entity));
        };
        registration.add = [id, size, alignment](EntityList& entities, std::uint32_t entity) {
            if (!entities.GetRegistry().valid(static_cast<entt::entity>(entity)))
            {
                return false;
            }
            ByteStorage* pool = Pool(entities, id, size, alignment);
            return pool != nullptr && pool->Emplace(static_cast<entt::entity>(entity)) != nullptr;
        };
        registration.remove = [id](EntityList& entities, std::uint32_t entity) {
            if (!entities.GetRegistry().valid(static_cast<entt::entity>(entity)))
            {
                return false;
            }
            if (ByteStorage* pool = ExistingPool(entities, id))
            {
                pool->remove(static_cast<entt::entity>(entity));
            }
            return true;
        };
        registration.pool = [id, size, alignment](EntityList& entities) -> entt::sparse_set* {
            return Pool(entities, id, size, alignment);
        };
        return registration;
    }
} // namespace Fenrir::Detail
