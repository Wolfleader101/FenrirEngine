#pragma once

#include <Iris/Iris.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirScripting/ComponentRegistry.hpp"

namespace Fenrir
{
    class CallContext;

    namespace Detail
    {
        class ChangeBuffer;
        struct CallContextAccess;
        struct EngineContext;

        // The active Scripting's call context, or null when no Scripting is running.
        [[nodiscard]] const CallContext* ActiveCallContext() noexcept;
    } // namespace Detail

    /**
     * @brief The change buffer, as a native function records into it (from CallContext::Changes): the structural
     * changes (creating and destroying entities, adding and removing components, inserting and removing resources) that
     * the engine's own entity functions record for C#, in one ordered list with theirs.
     *
     * Nothing here touches the storage when it is called. A created entity is alive at once, so its id can be returned
     * to C# and given components in the same call; everything else plays back, in the order it was recorded, when the
     * native code that called the script gets control back (after the calling C# system). A change naming an entity
     * that an earlier change destroyed is skipped with a warning. Entities and scene resources are the active scene's.
     */
    class ChangeBuffer
    {
      public:
        /**
         * @brief A new entity in the active scene, alive and usable at once (with no components yet): record changes on
         * it, or hand its id to C#. It gets its Transform, Name and Relationship at playback, before later changes.
         */
        [[nodiscard]] std::uint32_t CreateEntity() const;

        /** @brief Destroys `entity` at playback. False, recording nothing, if it is not alive now. */
        bool DestroyEntity(std::uint32_t entity) const;

        /**
         * @brief Gives `entity` `value` as its `T` at playback, replacing the one it has. False, recording nothing, if
         * the entity is not alive now.
         */
        template <typename T>
        bool AddComponent(std::uint32_t entity, T value = T{}) const
        {
            return RecordOn(entity, [value](Entity& target) { target.AddComponent<T>(value); });
        }

        /** @brief Takes `T` off `entity` at playback. False, recording nothing, if the entity is not alive now. */
        template <typename T>
        bool RemoveComponent(std::uint32_t entity) const
        {
            return RecordOn(entity, [](Entity& target) { target.RemoveComponent<T>(); });
        }

        /** @brief Makes `value` the active scene's `T` at playback (Scene::AddResource), replacing the one it has. */
        template <typename T>
        void InsertSceneResource(T value = T{}) const
        {
            Record([value](App& app) { app.GetActiveScene().AddResource<T>(value); });
        }

        /** @brief Removes the active scene's `T` at playback. */
        template <typename T>
        void RemoveSceneResource() const
        {
            Record([](App& app) { app.GetActiveScene().RemoveResource<T>(); });
        }

        /** @brief Makes `value` the App's `T` at playback (App::AddResource), replacing the one it has. */
        template <typename T>
        void InsertAppResource(T value = T{}) const
        {
            Record([value](App& app) { app.AddResource<T>(value); });
        }

        /** @brief Removes the App's `T` at playback. */
        template <typename T>
        void RemoveAppResource() const
        {
            Record([](App& app) { app.RemoveResource<T>(); });
        }

      private:
        friend class CallContext;
        ChangeBuffer(App& app, Detail::ChangeBuffer& changes) noexcept : m_app(&app), m_changes(&changes)
        {
        }

        // Records `change` for the entity `entity` of the active scene, unless it is not alive now. At playback it is
        // skipped if the entity has died since.
        bool RecordOn(std::uint32_t entity, std::function<void(Entity&)> change) const;

        // Records `change` to run at playback, in order with every other change.
        void Record(std::function<void(App&)> change) const;

        App* m_app;
        Detail::ChangeBuffer* m_changes;
    };

    /**
     * @brief What a native function receives first: the App the running Scripting serves, through it the active scene's
     * entities, and the change buffer. Only scripting makes one, and a native function is only ever called with the one
     * that is active.
     *
     * A native function may read and write component and resource data, but makes structural changes (creating or
     * destroying entities, adding or removing components, inserting or removing resources) only by recording them in
     * Changes(): the C# system that called it holds Ref<T>s into the engine's storage, and a structural change could
     * move what they point at.
     */
    class CallContext
    {
      public:
        /** @brief The App the running Scripting serves. */
        [[nodiscard]] App& GetApp() const noexcept
        {
            return *m_app;
        }

        /** @brief The active scene's entities, as they are now. Read and write data; never change them structurally. */
        [[nodiscard]] EntityList& Entities() const
        {
            return m_app->GetActiveScene().GetEntityList();
        }

        /** @brief The change buffer, where structural changes are recorded to play back after the calling system. */
        [[nodiscard]] ChangeBuffer Changes() const noexcept;

      private:
        friend struct Detail::CallContextAccess;
        CallContext(App& app, Detail::EngineContext& engine) noexcept : m_app(&app), m_engine(&engine)
        {
        }
        App* m_app;
        // Scripting's own state, for the engine's built-in native functions. Not part of the interface.
        Detail::EngineContext* m_engine;
    };

    namespace Detail
    {
        // The function Iris calls for the native function `Function`: it finds the active call context and calls
        // `Function` with it, or, when no Scripting is running, returns the "not running" value without calling it.
        template <auto Function, typename R, typename... Args>
        struct NativeFunctionCall
        {
            using Result = R;

            struct NoValue
            {
            };
            // What C# gets when no Scripting is running. Written only by Scripting::Install, when it registers the
            // native function, before .NET can call this.
            static inline std::conditional_t<std::is_void_v<R>, NoValue, R> notRunning{};

            static R Call(Args... arguments)
            {
                const CallContext* context = ActiveCallContext();
                if (context == nullptr)
                {
                    if constexpr (std::is_void_v<R>)
                    {
                        return;
                    }
                    else
                    {
                        return notRunning;
                    }
                }
                return Function(*context, arguments...);
            }
        };

        // NativeFunctionCall for a C++ function taking the call context first, noexcept or not. Matched on the
        // pointer's type, not the pointer: MSVC lets a noexcept pointer match both value specializations.
        template <auto Function, typename Pointer = decltype(Function)>
        struct NativeFunctionTraits;
        template <auto Function, typename R, typename... Args>
        struct NativeFunctionTraits<Function, R (*)(const CallContext&, Args...)>
            : NativeFunctionCall<Function, R, Args...>
        {
        };
        template <auto Function, typename R, typename... Args>
        struct NativeFunctionTraits<Function, R (*)(const CallContext&, Args...) noexcept>
            : NativeFunctionCall<Function, R, Args...>
        {
        };
    } // namespace Detail

    /**
     * @brief A C++ function that C# calls: a native function. It is declared by the full name of the C# method that
     * calls it ("Namespace.Type.Method", with `+` for a nested type), and listed in a ScriptingExtension.
     *
     * The C++ function's first parameter is the CallContext. The rest, and its result, are what C# passes and gets: any
     * Iris internal call type (sized numbers, `float`, `double`, `bool`, `char16_t`, pointers) and, as parameters,
     * `std::string_view`. A C++ exception that escapes it is stopped and logged by Iris.
     *
     * The C# side declares the field Iris binds under BindingName(). A C# field with no native function, or a different
     * signature, fails Scripting::Install with a diagnostic naming it.
     */
    class NativeFunction
    {
      public:
        /** @brief The native function `Function`, named `name`. When no Scripting is running C# gets a zero result. */
        template <auto Function>
        [[nodiscard]] static NativeFunction Make(std::string name)
        {
            using Traits = Detail::NativeFunctionTraits<Function>;
            return NativeFunction(std::move(name), [](std::string_view bindingName) {
                if constexpr (!std::is_void_v<typename Traits::Result>)
                {
                    Traits::notRunning = {};
                }
                return Iris::RegisterInternalCall<&Traits::Call>(bindingName);
            });
        }

        /**
         * @brief The native function `Function`, named `name`, which gives C# `whenNotRunning` if it is called when no
         * Scripting is running (it is not called then). The value takes effect when Scripting::Install registers the
         * function; if one C++ function is listed under two names, the value of the one registered last applies.
         */
        template <auto Function>
            requires(!std::is_void_v<typename Detail::NativeFunctionTraits<Function>::Result>)
        [[nodiscard]] static NativeFunction Make(std::string name,
                                                 typename Detail::NativeFunctionTraits<Function>::Result whenNotRunning)
        {
            using Traits = Detail::NativeFunctionTraits<Function>;
            return NativeFunction(std::move(name), [whenNotRunning](std::string_view bindingName) {
                Traits::notRunning = whenNotRunning;
                return Iris::RegisterInternalCall<&Traits::Call>(bindingName);
            });
        }

        /** @brief The full name of the C# method that calls it. */
        [[nodiscard]] const std::string& Name() const noexcept
        {
            return m_name;
        }

        /** @brief The name Iris binds a C# field by: BindingNameFor(Name()). */
        [[nodiscard]] std::string BindingName() const
        {
            return BindingNameFor(m_name);
        }

        /**
         * @brief The binding-name rule: the C# field for the native function "Namespace.Type.Method" is
         * `Namespace.Type.__native_Method`, a public static unmanaged function pointer field of the same type. Empty if
         * `name` has no type part or no method part.
         */
        [[nodiscard]] static std::string BindingNameFor(std::string_view name);

      private:
        friend class Scripting;
        using Registrar = std::function<Iris::CallResult<void>(std::string_view bindingName)>;

        NativeFunction(std::string name, Registrar registrar)
            : m_name(std::move(name)), m_register(std::move(registrar))
        {
        }

        // Sets the "not running" value and registers the function Iris calls under BindingName().
        [[nodiscard]] Iris::CallResult<void> Register() const
        {
            return m_register(BindingName());
        }

        std::string m_name;
        Registrar m_register;
    };

    /**
     * @brief What one engine package, or the app, gives scripting: its native functions, its components, resources and
     * events, and the shared assemblies that are its C# contract. Passed to Scripting::Install in
     * ScriptingConfig::extensions; scripting knows nothing of the package beyond this.
     */
    struct ScriptingExtension
    {
        /** @brief Who it is, for diagnostics. */
        std::string name;

        /**
         * @brief Its C# contract: assemblies every script load shares as one copy, after Fenrir.Math and the scripting
         * contract (which the built-in extension shares) and in extension order. A relative path is in the bridge
         * directory, beside the scripting contract, where a package's deploy function puts its contract.
         */
        std::vector<std::filesystem::path> sharedAssemblies;

        /** @brief The native functions its contract calls. Their names must be unique across every extension. */
        std::vector<NativeFunction> functions;

        /**
         * @brief Native components it exposes to C# (see MakeComponent). They are registered before the script assembly
         * loads and checked against it as it does. Their names must be unique across every extension, the engine's own
         * components (Fenrir.Transform, Fenrir.Name, Fenrir.Relationship) included.
         */
        std::vector<ComponentRegistration> components;

        /**
         * @brief C++ events it shares with C# (see MakeEvent), registered and checked as `components` are. Events only
         * C# defines need nothing here.
         */
        std::vector<EventRegistration> events;

        /**
         * @brief Native resources it exposes to C# (see MakeSceneResource and MakeAppResource), registered and checked
         * as `components` are. Their names must be unique across every extension.
         */
        std::vector<ResourceRegistration> resources;
    };
} // namespace Fenrir
