#pragma once

#include <Iris/Iris.hpp>

#include <cstddef>
#include <functional>
#include <string>
#include <type_traits>

#include "FenrirApp/App.hpp"
#include "FenrirECS/EntityList.hpp"

namespace Fenrir
{
    /**
     * @brief One component the engine exposes to C# by name: the name and C# struct it goes by, what scripts may do
     * with it, its ComponentType (which reaches its storage through an EntityList), and (for a native one) how to check
     * that the C# struct has exactly its layout.
     *
     * A native component is described by MakeComponent or MakeReadOnlyComponent and listed in the `components` of the
     * ScriptingExtension that gives it. A component a script defines in C# has no C++ type at all: it is registered,
     * with a dynamic ComponentType, when the script assembly loads. Copying one is cheap.
     */
    struct ComponentRegistration
    {
        /** The name C# uses: a [NativeComponent("...")] attribute for a native component, the type's full name
         * otherwise. */
        std::string name;

        /**
         * The full name of the C# struct this component is validated against (the mapping's typeName; for a
         * script-defined component, its own full name). C# gets the component only through that type: a script cannot
         * claim the name for a struct of its own, which would skip the layout check.
         */
        std::string typeName;

        /**
         * The component in the ECS: its size and alignment (which the C# struct must match), and the way to get, add,
         * remove, count and query it on any EntityList. Every registration has one.
         */
        ComponentType type;

        /** True if scripts may only read it: they cannot add or remove it, and C# gets it as a ReadOnlyRef. */
        bool readOnly = false;

        /**
         * True if the engine assumes every entity has it (Transform, Name, Relationship), so scripts may not remove it.
         */
        bool required = false;

        /** True for a component a script defines in C#. It exists only while the script assembly that defines it is
         * loaded. */
        bool scriptDefined = false;

        /**
         * Checks the C# struct this component mirrors against its Iris StructMapping, in a loaded script assembly. A
         * mismatch is InvalidArgument with a diagnostic naming the field. Empty for a script-defined component, whose
         * C# struct is the definition.
         */
        std::function<Iris::CallResult<void>(const Iris::Component&)> validate;
    };

    /**
     * @brief One event the engine exposes to C# by name. The event's queue is the App's named queue
     * (App::FindEventQueue), so C++ `SendEvent<T>`/`EventReader<T>` and C# `Events.Send`/`EventReader<T>` are the same
     * queue, and every reader in either language sees each event once.
     *
     * A C++ event is described by MakeEvent and listed in the `events` of the ScriptingExtension that shares it; its
     * C# struct says `[NativeEvent("Ns.Name")]`. An event only C# defines needs no registration at all: the script
     * assembly reports it as it loads and the engine makes its queue.
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

        /** True for an event a script defines in C#. It exists only while the script assembly that defines it is
         * loaded. */
        bool scriptDefined = false;

        /** Gives the App's queue for this event its name. Empty for a script-defined event, whose queue is made at
         * load. */
        std::function<bool(App& app)> bind;

        /** Checks the C# struct against the event's Iris StructMapping. Empty for a script-defined event. */
        std::function<Iris::CallResult<void>(const Iris::Component&)> validate;
    };

    /**
     * @brief Registers the C++ event `T` under `name`, mirrored by the C# struct that `Mapping`'s Iris::StructMapping
     * names. `T` must be copyable as plain bytes (the queue moves events between C++ and C# as bytes) and default
     * constructible. `Mapping` is `T` unless its members are not accessible to a mapping; see MakeComponent.
     */
    template <typename T, typename Mapping = T>
    [[nodiscard]] EventRegistration MakeEvent(std::string name)
    {
        static_assert(std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>,
                      "An event shared with scripts is moved as bytes, so it must be trivially copyable and default "
                      "constructible.");
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

    /**
     * @brief One shared struct: plain data a native function takes or returns by pointer that is neither a component
     * nor an event (a query filter, a hit). It has no storage and no id; it is listed only so its C# mirror is checked
     * against its Iris StructMapping when a script assembly loads, as a native component's is, and the load fails
     * naming the struct and field when they differ.
     *
     * Made by MakeSharedStruct and listed in the `sharedStructs` of the ScriptingExtension whose native functions use
     * it.
     */
    struct SharedStructRegistration
    {
        /** The name it goes by in diagnostics: the full name of its C# struct, "Namespace.Type". */
        std::string name;

        /** The full name of the C# struct it is validated against (the mapping's typeName). */
        std::string typeName;

        /** Size and alignment in bytes. */
        std::size_t size = 0;
        std::size_t alignment = 0;

        /** Checks the C# struct against the struct's Iris StructMapping. */
        std::function<Iris::CallResult<void>(const Iris::Component&)> validate;
    };

    /**
     * @brief Registers the shared struct `T` under `name`, mirrored by the C# struct that `Mapping`'s
     * Iris::StructMapping names. `T` must be copyable as plain bytes, since native functions pass it by pointer across
     * the boundary. `Mapping` is `T` unless its members are not accessible to a mapping; see MakeComponent.
     */
    template <typename T, typename Mapping = T>
    [[nodiscard]] SharedStructRegistration MakeSharedStruct(std::string name)
    {
        static_assert(std::is_trivially_copyable_v<T>,
                      "A shared struct crosses to C# by pointer as bytes, so it must be trivially copyable.");
        static_assert(sizeof(Mapping) == sizeof(T) && alignof(Mapping) == alignof(T),
                      "The mapping must have the size and alignment of the shared struct it stands for.");
        static_assert(Iris::StructValue<Mapping>,
                      "The mapping needs an Iris::StructMapping and must be trivially copyable standard-layout data.");
        SharedStructRegistration registration;
        registration.name = std::move(name);
        registration.typeName = std::string(Iris::StructMapping<Mapping>::typeName);
        registration.size = sizeof(T);
        registration.alignment = alignof(T);
        registration.validate = [](const Iris::Component& component) { return component.ValidateStruct<Mapping>(); };
        return registration;
    }

    /**
     * @brief Registers the native component `T` under `name`, mirrored by the C# struct that `Mapping`'s
     * Iris::StructMapping names. Scripts read and write the component's memory directly.
     *
     * `Mapping` is `T` itself unless `T`'s members are not accessible to a mapping (private, or a type Iris cannot
     * map), in which case it is a plain struct with exactly the same size and layout; the layout is then only as safe
     * as that claim, which is why the size and alignment are checked at compile time and the registry's tests read real
     * engine memory through it. `T` must be default constructible.
     */
    template <typename T, typename Mapping = T>
    [[nodiscard]] ComponentRegistration MakeComponent(std::string name)
    {
        static_assert(std::is_default_constructible_v<T>, "A component scripts can add must be default constructible.");
        static_assert(sizeof(Mapping) == sizeof(T) && alignof(Mapping) == alignof(T),
                      "The mapping must have the size and alignment of the component it stands for.");
        static_assert(Iris::StructValue<Mapping>,
                      "The mapping needs an Iris::StructMapping and must be trivially copyable standard-layout data.");
        return ComponentRegistration{
            .name = std::move(name),
            .typeName = std::string(Iris::StructMapping<Mapping>::typeName),
            .type = ComponentType::Native<T>(),
            .validate = [](const Iris::Component& component) { return component.ValidateStruct<Mapping>(); },
        };
    }

    /**
     * @brief Registers the native component `T` for reading only: scripts get a ReadOnlyRef, and cannot add or remove
     * it. Use it for data the engine keeps consistent itself, such as the entity hierarchy. See MakeComponent for
     * `Mapping`.
     */
    template <typename T, typename Mapping = T>
    [[nodiscard]] ComponentRegistration MakeReadOnlyComponent(std::string name)
    {
        auto registration = MakeComponent<T, Mapping>(std::move(name));
        registration.readOnly = true;
        return registration;
    }
} // namespace Fenrir
