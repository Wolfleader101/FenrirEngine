#pragma once

#include <Iris/CallResult.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirScripting/ComponentRegistry.hpp"

namespace Fenrir
{
    class CallContext;
    class Scripting;

    namespace Testing
    {
        class NativeCallHarness;
    }

    namespace Detail
    {
        class ChangeBuffer;
        class ExtensionSet;
        class IrisNativeRegistrar;
        class ScriptSystems;
        struct StageSession;

        template <auto Function, typename State, typename R, typename... Args>
        struct NativeFunctionCall;
        template <auto Function, typename Pointer = decltype(Function)>
        struct NativeFunctionTraits;
    } // namespace Detail

    /**
     * @brief The change buffer, as a native function records into it (from CallContext::Changes): the structural
     * changes (creating and destroying entities, parenting them, adding and removing components, inserting and removing
     * resources) that
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

        /**
         * @brief Makes `child` the last child of `parent` at playback (Entity::AddChild), as C++ parents an entity it
         * has just made: for a `child` with no parent, such as one CreateEntity made. False, recording nothing, if
         * either is not alive now; skipped at playback if either has died since.
         */
        bool AddChild(std::uint32_t parent, std::uint32_t child) const;

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
     * entities, the change buffer, and the name of the native function being called. One is built for each call, by
     * the registration's thunk, and a native function is only ever called with the one for its call. It carries nothing
     * of any extension: a native function that needs its package's state takes its extension state
     * (ScriptingExtension::SetState) as its second parameter.
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

        /**
         * @brief The native function being called, by the type-and-method part of its name: `PhysicsNative.Raycast`
         * for `Fenrir.Physics.PhysicsNative.Raycast` (NativeFunction::NativeNameFor). What WarnOnceRecord prefixes a
         * warning with, so a native function never spells its own name.
         */
        [[nodiscard]] std::string_view Native() const noexcept
        {
            return m_native;
        }

      private:
        friend class Scripting;
        friend class Testing::NativeCallHarness;
        template <auto Function, typename State, typename R, typename... Args>
        friend struct Detail::NativeFunctionCall;

        // `native` must outlive the context: the thunk's is the registration's, kept until Scripting shuts down.
        CallContext(App& app, Detail::ChangeBuffer& changes, std::string_view native) noexcept
            : m_app(&app), m_changes(&changes), m_native(native)
        {
        }
        App* m_app;
        Detail::ChangeBuffer* m_changes;
        std::string_view m_native;
    };

    /**
     * @brief A C++ function that C# calls: a native function. It is declared by the full name of the C# method that
     * calls it ("Namespace.Type.Method", with `+` for a nested type), and listed in a ScriptingExtension.
     *
     * The C++ function takes one of two forms:
     * - `R F(const CallContext&, Args...)`, a stateless native function;
     * - `R F(const CallContext&, State&, Args...)`, where `State` is a class type: it also receives its extension's
     *   state, the object ScriptingExtension::SetState was given. Scripting::Install fails, naming the function and
     * both types, when the extension that lists it carries no state or a state of another type.
     *
     * `Args` and `R` are what C# passes and gets: any Iris internal call type (sized numbers, `float`, `double`,
     * `bool`, `char16_t`, pointers, a shared struct or component by pointer, an enum by value as its underlying
     * integer, a wrapper by value as its one field, see Iris::WrapperOf) and, as parameters, `std::string_view`. A C++
     * exception that escapes it is stopped and logged by Iris.
     *
     * The C# side declares the field Iris binds under BindingName(). A C# field with no native function, or a different
     * signature, fails Scripting::Install with a diagnostic naming it.
     *
     * Make is declared here and defined in NativeFunctionMake.hpp, with the thunk Iris calls: a registration (a
     * generated natives header, or a hand-written maker) includes that header; everything else needs only this one.
     */
    class NativeFunction
    {
      public:
        /** @brief The native function `Function`, named `name`. When no Scripting is running C# gets a zero result. */
        template <auto Function>
        [[nodiscard]] static NativeFunction Make(std::string name);

        /**
         * @brief The native function `Function`, named `name`, which gives C# `whenNotRunning` if it is called when no
         * Scripting is running (it is not called then). The value takes effect when Scripting::Install registers the
         * function; if one C++ function is listed under two names, the value of the one registered last applies.
         */
        template <auto Function>
            requires(!std::is_void_v<typename Detail::NativeFunctionTraits<Function>::Result>)
        [[nodiscard]] static NativeFunction Make(
            std::string name, typename Detail::NativeFunctionTraits<Function>::Result whenNotRunning);

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

        /**
         * @brief The name a call context carries for the native function "Namespace.Type.Method": its type-and-method
         * part, `Type.Method` (`Outer+Inner.Method` for a nested type), with the namespace dropped because a
         * WarnOnceRecord already names the extension. `name` itself when it has no namespace.
         */
        [[nodiscard]] static std::string_view NativeNameFor(std::string_view name) noexcept;

      private:
        friend class Scripting;
        friend class Detail::ExtensionSet;        // StateType, for the extension state rule.
        friend class Detail::IrisNativeRegistrar; // Register, the one adapter at the NativeRegistrar seam over Iris.
        // The native call harness registers a function as the registrar does and calls it through its thunk.
        friend class Testing::NativeCallHarness;
        using Registrar = std::function<Iris::CallResult<void>(std::string_view bindingName,
                                                               std::string_view nativeName, void* state)>;

        // `thunk` is the function Iris calls (NativeFunctionMake.hpp's thunk for Function), of the signature
        // `signature` names: R(Args...), the C# signature, with the context and the state dropped.
        NativeFunction(std::string name, std::optional<std::type_index> stateType, Registrar registrar, void* thunk,
                       std::type_index signature)
            : m_name(std::move(name)), m_stateType(stateType), m_register(std::move(registrar)), m_thunk(thunk),
              m_signature(signature)
        {
        }

        // The thunk Register installs, as a plain pointer: Thunk() cast to `R (*)(Args...)` is the function Iris
        // calls, when Signature() is typeid(R(Args...)).
        [[nodiscard]] void* Thunk() const noexcept
        {
            return m_thunk;
        }

        // The thunk's signature R(Args...): what C# declares, with the call context and the extension state dropped.
        [[nodiscard]] const std::type_index& Signature() const noexcept
        {
            return m_signature;
        }

        // Sets the "not running" value, the extension state (`state`, of the type StateType() names, or null for a
        // stateless function) and the name each call's context carries (NativeNameFor(Name())), and registers the
        // function Iris calls under BindingName().
        [[nodiscard]] Iris::CallResult<void> Register(void* state) const
        {
            return m_register(BindingName(), NativeNameFor(m_name), state);
        }

        // The type of extension state it takes, or none for a stateless native function.
        [[nodiscard]] const std::optional<std::type_index>& StateType() const noexcept
        {
            return m_stateType;
        }

        std::string m_name;
        std::optional<std::type_index> m_stateType;
        Registrar m_register;
        void* m_thunk;
        std::type_index m_signature;
    };

    /**
     * @brief One group of a script's stage systems: the C# systems that join one extension stage at one order
     * (`[StageSystem(name, Order = n)]`), as the extension that declares the stage is given it to place in its own
     * work. A copy is the same group. It is good while the script generation that has it is loaded: once the script is
     * released (ScriptingExtension::removeStages runs then), Run does nothing.
     */
    class StageGroup
    {
      public:
        /** @brief The stage's full name, as the extension declares it. */
        [[nodiscard]] const std::string& Stage() const noexcept
        {
            return m_stage;
        }

        /** @brief The order every system of the group gave. */
        [[nodiscard]] std::int32_t Order() const noexcept
        {
            return m_order;
        }

        /**
         * @brief Runs the group's systems, in name order, in one call into the script; off the main thread one error
         * and nothing runs. A system that throws has its changes dropped and is disabled until the next reload. The
         * structural changes the others record wait for StageChanges::PlayBack.
         */
        void Run() const;

      private:
        friend class Detail::ScriptSystems;
        StageGroup(std::shared_ptr<Detail::StageSession> session, std::string stage, std::int32_t order,
                   std::int32_t index)
            : m_session(std::move(session)), m_stage(std::move(stage)), m_order(order), m_index(index)
        {
        }

        std::shared_ptr<Detail::StageSession> m_session;
        std::string m_stage;
        std::int32_t m_order;
        std::int32_t m_index; // The script's number for the group, which it runs it by.
    };

    /**
     * @brief The structural changes a script's stage systems record, as the extension that runs them plays them back.
     * Good as long as the StageGroups it came with.
     */
    class StageChanges
    {
      public:
        /**
         * @brief Plays back, in order, the structural changes recorded since the last playback. The extension calls it
         * when the work hosting its groups returns, never between two groups, so its own work never sees entities or
         * components appear or vanish halfway through. Nothing once the script is released.
         */
        void PlayBack() const;

      private:
        friend class Detail::ScriptSystems;
        explicit StageChanges(std::shared_ptr<Detail::StageSession> session) : m_session(std::move(session))
        {
        }

        std::shared_ptr<Detail::StageSession> m_session;
    };

    /**
     * @brief What one engine package, or the app, gives scripting: its native functions, its components, events,
     * shared structs and resources, its extension state, the shared assemblies that are its C# contract, and the
     * extension stages it hosts. Passed to Scripting::Install in ScriptingConfig::extensions; scripting knows nothing
     * of the package beyond this.
     */
    struct ScriptingExtension
    {
        /**
         * @brief Places the groups of one script generation that join this extension's stages: one item per group in
         * the extension's own work, at the group's order. Equal orders run the extension's C++ work first. The groups
         * come sorted by stage name, then by order; a stage no system joins has none, and the function is called even
         * with none.
         */
        using PlaceStages = std::function<void(App& app, std::vector<StageGroup> groups, StageChanges changes)>;

        /** @brief Removes every item PlaceStages placed, when the script generation is released. */
        using RemoveStages = std::function<void(App& app)>;

        /**
         * @brief Gives the extension its extension state: the object its native functions of the form
         * `R F(const CallContext&, State&, Args...)` receive. One state per extension; setting it again replaces it.
         * Scripting keeps it alive until it shuts down.
         */
        template <class State>
        void SetState(std::shared_ptr<State> state)
        {
            m_stateType = std::type_index(typeid(State));
            m_state = std::move(state);
        }

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
         * @brief Shared structs (see MakeSharedStruct): the plain data its native functions take or return by pointer
         * that is neither a component nor an event. Each is checked against its C# struct when a script assembly loads,
         * as `components` are. The names of every extension's components, events and shared structs together must be
         * unique.
         */
        std::vector<SharedStructRegistration> sharedStructs;

        /**
         * @brief Native resources it exposes to C# (see MakeSceneResource and MakeAppResource), registered and checked
         * as `components` are. Their names are unique with the components', events' and shared structs' of every
         * extension.
         */
        std::vector<ResourceRegistration> resources;

        /**
         * @brief The extension stages it hosts, by full name (`Fenrir.Skeleton.Modify`): named parts of its own C++
         * work that C# systems join with `[StageSystem(name, Order = n)]`. A name is declared by one extension only. A
         * script whose stage system names a stage no extension declares fails to load. Like its native functions, a
         * stage needs no installed package: a stage that never runs is not an error.
         */
        std::vector<std::string> stages;

        /** @brief Places a loaded script's groups for `stages`. Required when `stages` is not empty. */
        PlaceStages placeStages;

        /** @brief Removes what placeStages placed. Required when `stages` is not empty. */
        RemoveStages removeStages;

      private:
        friend class Detail::ExtensionSet;
        // The native call harness registers an extension's functions with its state, as the extension set does.
        friend class Testing::NativeCallHarness;
        std::shared_ptr<void> m_state;
        std::optional<std::type_index> m_stateType;
    };
} // namespace Fenrir
