#pragma once

#include <Iris/Iris.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <type_traits>

#include "FenrirApp/App.hpp"
#include "FenrirECS/Entity.hpp"

namespace Fenrir
{
    /**
     * @brief One component the engine exposes to C# by name: how to reach its storage for an entity, and (for a native one)
     * how to check that a C# struct has exactly its layout.
     *
     * EnTT has no registry of types by name at run time, so this is the table that maps a name such as
     * "Fenrir.Transform" to the operations on the right storage. A native component is described by MakeComponent or
     * MakeReadOnlyComponent and registered with Scripting::RegisterComponent or ScriptingConfig::components. A component a
     * script defines in C# has no C++ type at all: it is registered when the script assembly loads. Copying one is cheap.
     */
    struct ComponentRegistration
    {
        /** The name C# uses: a [NativeComponent("...")] attribute for a native component, the type's full name otherwise. */
        std::string name;

        /**
         * The full name of the C# struct this component is validated against (the mapping's typeName; for a script-defined
         * component, its own full name). C# gets the component only through that type: a script cannot claim the name for a
         * struct of its own, which would skip the layout check.
         */
        std::string typeName;

        /** True if scripts may only read it: it has no add or remove, and C# gets it as a ReadOnlyRef. */
        bool readOnly = false;

        /**
         * True if the engine assumes every entity has it (Transform, Name, Relationship), so scripts may not remove it.
         */
        bool required = false;

        /** True for a component a script defines in C#. It exists only while the script assembly that defines it is loaded. */
        bool scriptDefined = false;

        /** Size and alignment in bytes of one component, as C# lays it out. */
        std::size_t size = 0;
        std::size_t alignment = 0;

        /** Where the component is in the entity's storage, or null if the entity is not alive or lacks it. */
        std::function<void*(EntityList& entities, std::uint32_t entity)> get;

        /** Whether the entity is alive and has the component. */
        std::function<bool(EntityList& entities, std::uint32_t entity)> has;

        /** Adds a default-constructed (for a script component, zeroed) component. False for a read-only one or a dead entity. */
        std::function<bool(EntityList& entities, std::uint32_t entity)> add;

        /** Removes the component. False for a read-only one or a dead entity. */
        std::function<bool(EntityList& entities, std::uint32_t entity)> remove;

        /** Names this component in a query (EntityList::ForEachWith), beside any other component, native or dynamic. */
        std::optional<ComponentSelector> selector;

        /** How many entities in the list have this component. */
        std::function<std::size_t(EntityList& entities)> count;

        /**
         * Checks the C# struct this component mirrors against its Iris StructMapping, in a loaded script assembly. A mismatch
         * is InvalidArgument with a diagnostic naming the field. Empty for a script-defined component, whose C# struct is
         * the definition.
         */
        std::function<Iris::CallResult<void>(const Iris::Component&)> validate;
    };

    /**
     * @brief One event the engine exposes to C# by name. The event's queue is the App's named queue (App::FindEventQueue), so
     * C++ `SendEvent<T>`/`ReadEvents<T>` and C# `Events.Send/Read` are the same queue with the same frame window.
     *
     * A C++ event is described by MakeEvent and registered with Scripting::RegisterEvent or ScriptingConfig::events; its C#
     * struct says `[NativeEvent("Ns.Name")]`. An event only C# defines needs no registration at all: the script assembly
     * reports it as it loads and the engine makes its queue.
     */
    struct EventRegistration
    {
        /** The name C# uses: a [NativeEvent("...")] attribute for a native event, the type's full name otherwise. */
        std::string name;

        /** The full name of the C# struct this event is validated against; C# gets the event only through that type. */
        std::string typeName;

        /** Size and alignment in bytes of one event. */
        std::size_t size = 0;
        std::size_t alignment = 0;

        /** True for an event a script defines in C#. It exists only while the script assembly that defines it is loaded. */
        bool scriptDefined = false;

        /** Gives the App's queue for this event its name. Empty for a script-defined event, whose queue is made at load. */
        std::function<bool(App& app)> bind;

        /** Checks the C# struct against the event's Iris StructMapping. Empty for a script-defined event. */
        std::function<Iris::CallResult<void>(const Iris::Component&)> validate;
    };

    /**
     * @brief Registers the C++ event `T` under `name`, mirrored by the C# struct that `Mapping`'s Iris::StructMapping names.
     * `T` must be copyable as plain bytes (the queue moves events between C++ and C# as bytes) and default constructible.
     * `Mapping` is `T` unless its members are not accessible to a mapping; see MakeComponent.
     */
    template <typename T, typename Mapping = T>
    [[nodiscard]] EventRegistration MakeEvent(std::string name)
    {
        static_assert(std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>,
                      "An event shared with scripts is moved as bytes, so it must be trivially copyable and default constructible.");
        static_assert(sizeof(Mapping) == sizeof(T) && alignof(Mapping) == alignof(T),
                      "The mapping must have the size and alignment of the event it stands for.");
        static_assert(Iris::StructValue<Mapping>,
                      "The mapping needs an Iris::StructMapping and must be trivially copyable standard-layout data.");
        EventRegistration registration;
        registration.name = std::move(name);
        registration.typeName = std::string(Iris::StructMapping<Mapping>::typeName);
        registration.size = sizeof(T);
        registration.alignment = alignof(T);
        registration.bind = [eventName = registration.name](App& app) { return app.NameEvent<T>(eventName); };
        registration.validate = [](const Iris::Component& component) { return component.ValidateStruct<Mapping>(); };
        return registration;
    }

    namespace Detail
    {
        // The entity with this value, if it is alive. Not `Entity(id, &list)`: that constructor creates an entity's
        // Transform, Name and Relationship, replacing whatever is there.
        inline Entity LiveEntity(EntityList& entities, std::uint32_t entity)
        {
            return entities.GetEntity(entity);
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
        registration.size = sizeof(T);
        registration.alignment = alignof(T);
        registration.get = [](EntityList& entities, std::uint32_t entity) -> void* {
            Entity handle = Detail::LiveEntity(entities, entity);
            return handle.IsValid() && handle.HasComponent<T>() ? &handle.GetComponent<T>() : nullptr;
        };
        registration.has = [](EntityList& entities, std::uint32_t entity) {
            Entity handle = Detail::LiveEntity(entities, entity);
            return handle.IsValid() && handle.HasComponent<T>();
        };
        registration.add = [](EntityList& entities, std::uint32_t entity) {
            Entity handle = Detail::LiveEntity(entities, entity);
            if (!handle.IsValid())
            {
                return false;
            }
            if (!handle.HasComponent<T>())
            {
                handle.AddComponent<T>();
            }
            return true;
        };
        registration.remove = [](EntityList& entities, std::uint32_t entity) {
            Entity handle = Detail::LiveEntity(entities, entity);
            if (!handle.IsValid())
            {
                return false;
            }
            if (handle.HasComponent<T>())
            {
                handle.RemoveComponent<T>();
            }
            return true;
        };
        registration.selector = ComponentSelector::Native<T>();
        registration.count = [selector = *registration.selector](EntityList& entities) { return entities.CountOf(selector); };
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
