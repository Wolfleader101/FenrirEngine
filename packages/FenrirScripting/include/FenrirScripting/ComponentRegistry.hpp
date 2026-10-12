#pragma once

#include <Iris/Iris.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>

#include "FenrirECS/Entity.hpp"

namespace Fenrir
{
    /**
     * @brief One native component the engine exposes to C# by name: how to reach its storage for an entity, and how to
     * check that a C# struct has exactly its layout.
     *
     * EnTT has no registry of types by name at run time, so this is the table that maps a name such as
     * "Fenrir.Transform" to the operations on the right storage. Build one with MakeComponent or MakeReadOnlyComponent
     * and register it with Scripting::RegisterComponent or ScriptingConfig::components. It holds no state, so it can be
     * copied freely.
     */
    struct ComponentRegistration
    {
        /** The name C# uses, in a [NativeComponent("...")] attribute. */
        std::string name;

        /**
         * The full name of the C# struct this component is validated against (the mapping's typeName). C# gets the
         * component only through that type: a script cannot claim the name for a struct of its own, which would skip the
         * layout check.
         */
        std::string typeName;

        /** True if scripts may only read it: it has no add or remove, and C# gets it as a ReadOnlyRef. */
        bool readOnly = false;

        /** Where the component is in the entity's storage, or null if the entity is not alive or lacks it. */
        void* (*get)(EntityList& entities, std::uint32_t entity) = nullptr;

        /** Whether the entity is alive and has the component. */
        bool (*has)(EntityList& entities, std::uint32_t entity) = nullptr;

        /** Adds a default-constructed component. False for a read-only one or a dead entity. */
        bool (*add)(EntityList& entities, std::uint32_t entity) = nullptr;

        /** Removes the component. False for a read-only one or a dead entity. */
        bool (*remove)(EntityList& entities, std::uint32_t entity) = nullptr;

        /**
         * Checks the C# struct this component mirrors against its Iris StructMapping, in a loaded script assembly.
         * A mismatch is InvalidArgument with a diagnostic naming the field.
         */
        std::function<Iris::CallResult<void>(const Iris::Component&)> validate;
    };

    namespace Detail
    {
        // The entity with this value, if it is alive. Not `Entity(id, &list)`: that constructor creates an entity's
        // Transform, Name and Relationship, replacing whatever is there.
        inline Entity LiveEntity(EntityList& entities, std::uint32_t entity)
        {
            return entities.GetEntity(entity);
        }

        template <typename T>
        void* GetComponentPointer(EntityList& entities, std::uint32_t entity)
        {
            Entity handle = LiveEntity(entities, entity);
            if (!handle.IsValid() || !handle.HasComponent<T>())
            {
                return nullptr;
            }
            return &handle.GetComponent<T>();
        }

        template <typename T>
        bool HasComponentOf(EntityList& entities, std::uint32_t entity)
        {
            Entity handle = LiveEntity(entities, entity);
            return handle.IsValid() && handle.HasComponent<T>();
        }

        template <typename T>
        bool AddComponentOf(EntityList& entities, std::uint32_t entity)
        {
            Entity handle = LiveEntity(entities, entity);
            if (!handle.IsValid())
            {
                return false;
            }
            if (!handle.HasComponent<T>())
            {
                handle.AddComponent<T>();
            }
            return true;
        }

        template <typename T>
        bool RemoveComponentOf(EntityList& entities, std::uint32_t entity)
        {
            Entity handle = LiveEntity(entities, entity);
            if (!handle.IsValid())
            {
                return false;
            }
            if (handle.HasComponent<T>())
            {
                handle.RemoveComponent<T>();
            }
            return true;
        }
    } // namespace Detail

    /**
     * @brief Registers the native component `T` under `name`, mirrored by the C# struct that `Mapping`'s
     * Iris::StructMapping names. Scripts read and write the component's memory directly.
     *
     * `Mapping` is `T` itself unless `T`'s members are not accessible to a mapping (private, or a type Iris cannot map),
     * in which case it is a plain struct with exactly the same size and layout; the layout is then only as safe as that
     * claim, which is why the size and alignment are checked at compile time and the registry's tests read real engine
     * memory through it. `T` must be default constructible.
     */
    template <typename T, typename Mapping = T>
    [[nodiscard]] ComponentRegistration MakeComponent(std::string name)
    {
        static_assert(std::is_default_constructible_v<T>, "A component scripts can add must be default constructible.");
        static_assert(sizeof(Mapping) == sizeof(T) && alignof(Mapping) == alignof(T),
                      "The mapping must have the size and alignment of the component it stands for.");
        static_assert(Iris::StructValue<Mapping>,
                      "The mapping needs an Iris::StructMapping and must be trivially copyable standard-layout data.");
        ComponentRegistration registration;
        registration.name = std::move(name);
        registration.typeName = std::string(Iris::StructMapping<Mapping>::typeName);
        registration.readOnly = false;
        registration.get = &Detail::GetComponentPointer<T>;
        registration.has = &Detail::HasComponentOf<T>;
        registration.add = &Detail::AddComponentOf<T>;
        registration.remove = &Detail::RemoveComponentOf<T>;
        registration.validate = [](const Iris::Component& component) { return component.ValidateStruct<Mapping>(); };
        return registration;
    }

    /**
     * @brief Registers the native component `T` for reading only: scripts get a ReadOnlyRef, and cannot add or remove it.
     * Use it for data the engine keeps consistent itself, such as the entity hierarchy. See MakeComponent for `Mapping`.
     */
    template <typename T, typename Mapping = T>
    [[nodiscard]] ComponentRegistration MakeReadOnlyComponent(std::string name)
    {
        auto registration = MakeComponent<T, Mapping>(std::move(name));
        registration.readOnly = true;
        registration.add = nullptr;
        registration.remove = nullptr;
        return registration;
    }
} // namespace Fenrir
