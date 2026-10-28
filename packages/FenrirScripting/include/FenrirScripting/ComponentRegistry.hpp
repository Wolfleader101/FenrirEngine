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
     * @brief What every registration shares: one C++ type the engine exposes to C# by name, mirrored by a C# struct
     * that is checked against the type's Iris StructMapping when a script assembly loads. A component, event, shared
     * struct or resource registration is one of these with its kind's extras; the registry's tables hold them under
     * the one rule that a name names one thing across every kind. Made by MakeMirror, which the kinds' makers build on.
     * Copying one is cheap.
     */
    struct MirrorRegistration
    {
        /**
         * The name C# uses: a [NativeComponent("...")], [NativeEvent("...")] or [NativeResource("...")] attribute for a
         * native one, the type's full name for a script-defined one or a shared struct.
         */
        std::string name;

        /**
         * The full name of the C# struct it is validated against (the mapping's typeName; for a script-defined one,
         * its own full name). C# gets it only through that type: a script cannot claim the name for a struct of its
         * own, which would skip the layout check.
         */
        std::string typeName;

        /** Size and alignment in bytes, which the C# struct must match. */
        std::size_t size = 0;
        std::size_t alignment = 0;

        /** True for a component or event a script defines in C#. It exists only while the script assembly that defines
         * it is loaded. Shared structs and resources are never script-defined. */
        bool scriptDefined = false;

        /**
         * Checks the C# struct against the Iris StructMapping, in a loaded script assembly. A mismatch is
         * InvalidArgument with a diagnostic naming the field. Empty for a script-defined one, whose C# struct is the
         * definition.
         */
        std::function<Iris::CallResult<void>(const Iris::Component&)> validate;
    };

    /**
     * @brief The registration of `T` under `name`, mirrored by the C# struct that `Mapping`'s Iris::StructMapping
     * names: what the kinds' makers share. `Mapping` is `T` itself unless `T`'s members are not accessible to a mapping
     * (private, or a type Iris cannot map), in which case it is a plain struct with exactly the same size and layout;
     * the layout is then only as safe as that claim, which is why the size and alignment are checked at compile time
     * and the registry's tests read real engine memory through it.
     */
    template <typename T, typename Mapping = T>
    [[nodiscard]] MirrorRegistration MakeMirror(std::string name)
    {
        static_assert(sizeof(Mapping) == sizeof(T) && alignof(Mapping) == alignof(T),
                      "The mapping must have the size and alignment of the type it stands for.");
        static_assert(Iris::StructValue<Mapping>,
                      "The mapping needs an Iris::StructMapping and must be trivially copyable standard-layout data.");
        MirrorRegistration registration;
        registration.name = std::move(name);
        registration.typeName = std::string(Iris::StructMapping<Mapping>::typeName);
        registration.size = sizeof(T);
        registration.alignment = alignof(T);
        registration.validate = [](const Iris::Component& component) { return component.ValidateStruct<Mapping>(); };
        return registration;
    }

    /**
     * @brief One component the engine exposes to C# by name: its MirrorRegistration, what scripts may do with it, and
     * its ComponentType (which reaches its storage through an EntityList).
     *
     * A native component is described by MakeComponent or MakeReadOnlyComponent and listed in the `components` of the
     * ScriptingExtension that gives it. A component a script defines in C# has no C++ type at all: it is registered,
     * with a dynamic ComponentType, when the script assembly loads.
     */
    struct ComponentRegistration : MirrorRegistration
    {
        /**
         * The component in the ECS: its size and alignment (the registration's, which the C# struct must match), and
         * the way to get, add, remove, count and query it on any EntityList. Every registration has one.
         */
        ComponentType type;

        /** True if scripts may only read it: they cannot add or remove it, and C# gets it as a ReadOnlyRef. */
        bool readOnly = false;

        /**
         * True if the engine assumes every entity has it (Transform, Name, Relationship), so scripts may not remove it.
         */
        bool required = false;
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
    struct EventRegistration : MirrorRegistration
    {
        /** Gives the App's queue for this event its name. Empty for a script-defined event, whose queue is made at
         * load. */
        std::function<bool(App& app)> bind;
    };

    /**
     * @brief Registers the C++ event `T` under `name`, mirrored by the C# struct that `Mapping`'s Iris::StructMapping
     * names. `T` must be copyable as plain bytes (the queue moves events between C++ and C# as bytes) and default
     * constructible. `Mapping` is `T` unless its members are not accessible to a mapping; see MakeMirror.
     */
    template <typename T, typename Mapping = T>
    [[nodiscard]] EventRegistration MakeEvent(std::string name)
    {
        static_assert(std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>,
                      "An event shared with scripts is moved as bytes, so it must be trivially copyable and default "
                      "constructible.");
        EventRegistration registration{MakeMirror<T, Mapping>(std::move(name))};
        registration.bind = [eventName = registration.name](App& app) { return app.NameEvent<T>(eventName); };
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
    struct SharedStructRegistration : MirrorRegistration
    {
    };

    /**
     * @brief Registers the shared struct `T` under `name`, mirrored by the C# struct that `Mapping`'s
     * Iris::StructMapping names. `T` must be copyable as plain bytes, since native functions pass it by pointer across
     * the boundary. `Mapping` is `T` unless its members are not accessible to a mapping; see MakeMirror.
     */
    template <typename T, typename Mapping = T>
    [[nodiscard]] SharedStructRegistration MakeSharedStruct(std::string name)
    {
        static_assert(std::is_trivially_copyable_v<T>,
                      "A shared struct crosses to C# by pointer as bytes, so it must be trivially copyable.");
        return SharedStructRegistration{MakeMirror<T, Mapping>(std::move(name))};
    }

    /**
     * @brief Registers the native component `T` under `name`, mirrored by the C# struct that `Mapping`'s
     * Iris::StructMapping names. Scripts read and write the component's memory directly. `T` must be default
     * constructible. `Mapping` is `T` unless its members are not accessible to a mapping; see MakeMirror.
     */
    template <typename T, typename Mapping = T>
    [[nodiscard]] ComponentRegistration MakeComponent(std::string name)
    {
        static_assert(std::is_default_constructible_v<T>, "A component scripts can add must be default constructible.");
        return ComponentRegistration{MakeMirror<T, Mapping>(std::move(name)), ComponentType::Native<T>()};
    }

    /**
     * @brief Registers the native component `T` for reading only: scripts get a ReadOnlyRef, and cannot add or remove
     * it. Use it for data the engine keeps consistent itself, such as the entity hierarchy. See MakeMirror for
     * `Mapping`.
     */
    template <typename T, typename Mapping = T>
    [[nodiscard]] ComponentRegistration MakeReadOnlyComponent(std::string name)
    {
        auto registration = MakeComponent<T, Mapping>(std::move(name));
        registration.readOnly = true;
        return registration;
    }

    /** @brief Where a native resource lives: in each scene (C# reaches the active scene's) or once in the App. */
    enum class ResourceScope
    {
        Scene,
        App,
    };

    /**
     * @brief One native resource the engine exposes to C# by name (ADR-0029): a plain-data scene or app resource that
     * C# reaches through `Resources.Get<T>()` as a Ref into the engine's storage. Its C# struct carries
     * `[NativeResource("...")]` with the same name.
     *
     * Described by MakeSceneResource, MakeAppResource or their read-only variants and listed in the `resources` of the
     * ScriptingExtension that gives it. A resource with behaviour (an input device, debug drawing) is not one: C# gets
     * a facade of native functions instead. Copying one is cheap.
     */
    struct ResourceRegistration : MirrorRegistration
    {
        /** Whether each scene has one (C# reaches the active scene's) or the App has one. */
        ResourceScope scope = ResourceScope::Scene;

        /** True if scripts may only read it: they get a ReadOnlyRef, and cannot insert or remove it. */
        bool readOnly = false;

        /** The resource in `app` (the active scene's, for a scene resource), or null when there is none now. */
        std::function<void*(App& app)> find;

        /**
         * Makes a default resource in `app` (in the active scene, for a scene resource), replacing any there is, and
         * returns it. Called only when changes play back.
         */
        std::function<void*(App& app)> insert;

        /** Removes the resource from `app` (from the active scene, for a scene resource); nothing if there is none. */
        std::function<void(App& app)> remove;
    };

    namespace Detail
    {
        // What every Make...Resource shares: the checks on `T` and its mapping, and how to reach `T` in its scope.
        template <typename T, typename Mapping>
        [[nodiscard]] ResourceRegistration MakeResource(std::string name, ResourceScope scope, bool readOnly)
        {
            static_assert(std::is_default_constructible_v<T>,
                          "A resource scripts can insert must be default constructible.");
            ResourceRegistration registration{MakeMirror<T, Mapping>(std::move(name))};
            registration.scope = scope;
            registration.readOnly = readOnly;
            if (scope == ResourceScope::Scene)
            {
                registration.find = [](App& app) -> void* { return app.GetActiveScene().GetResource<T>(); };
                registration.insert = [](App& app) -> void* { return &app.GetActiveScene().AddResource<T>(); };
                registration.remove = [](App& app) { app.GetActiveScene().RemoveResource<T>(); };
            }
            else
            {
                registration.find = [](App& app) -> void* { return app.TryGetResource<T>(); };
                registration.insert = [](App& app) -> void* { return &app.AddResource<T>(); };
                registration.remove = [](App& app) { app.RemoveResource<T>(); };
            }
            return registration;
        }
    } // namespace Detail

    /**
     * @brief Registers the scene resource `T` (one per scene, Scene::GetResource) under `name`, mirrored by the C#
     * struct that `Mapping`'s Iris::StructMapping names. C# reads and writes the active scene's resource in place, and
     * may insert and remove it. `T` must be trivially copyable (C# writes it as bytes) and default constructible; see
     * MakeMirror for `Mapping`.
     */
    template <typename T, typename Mapping = T>
    [[nodiscard]] ResourceRegistration MakeSceneResource(std::string name)
    {
        static_assert(std::is_trivially_copyable_v<T>, "A resource scripts write is written as bytes.");
        return Detail::MakeResource<T, Mapping>(std::move(name), ResourceScope::Scene, false);
    }

    /**
     * @brief Registers the scene resource `T` for reading only: C# gets a ReadOnlyRef and cannot insert or remove it.
     * Otherwise as MakeSceneResource.
     */
    template <typename T, typename Mapping = T>
    [[nodiscard]] ResourceRegistration MakeReadOnlySceneResource(std::string name)
    {
        return Detail::MakeResource<T, Mapping>(std::move(name), ResourceScope::Scene, true);
    }

    /**
     * @brief Registers the app resource `T` (one per App, App::TryGetResource) under `name`. Otherwise as
     * MakeSceneResource.
     */
    template <typename T, typename Mapping = T>
    [[nodiscard]] ResourceRegistration MakeAppResource(std::string name)
    {
        static_assert(std::is_trivially_copyable_v<T>, "A resource scripts write is written as bytes.");
        return Detail::MakeResource<T, Mapping>(std::move(name), ResourceScope::App, false);
    }

    /**
     * @brief Registers the app resource `T` for reading only: C# gets a ReadOnlyRef and cannot insert or remove it.
     * Otherwise as MakeAppResource.
     */
    template <typename T, typename Mapping = T>
    [[nodiscard]] ResourceRegistration MakeReadOnlyAppResource(std::string name)
    {
        return Detail::MakeResource<T, Mapping>(std::move(name), ResourceScope::App, true);
    }
} // namespace Fenrir
