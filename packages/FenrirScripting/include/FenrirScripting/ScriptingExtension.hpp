#pragma once

#include <Iris/Iris.hpp>

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
#include "FenrirECS/EntityList.hpp"
#include "FenrirScripting/ComponentRegistry.hpp"

namespace Fenrir
{
    class CallContext;
    class Scripting;

    namespace Detail
    {
        // The active Scripting's call context, or null when no Scripting is running.
        [[nodiscard]] const CallContext* ActiveCallContext() noexcept;
    } // namespace Detail

    /**
     * @brief What a native function receives first: the App the running Scripting serves, and through it the active
     * scene's entities. Only scripting makes one, and a native function is only ever called with the one that is
     * active. It carries nothing of any extension: a native function that needs its package's state takes its
     * extension state (ScriptingExtension::SetState) as its second parameter.
     *
     * A native function may read and write component data, but must not make structural changes (create or destroy
     * entities, add or remove components): the C# system that called it holds Ref<T>s into the ECS storage, and a
     * structural change could move what they point at.
     */
    class CallContext
    {
      public:
        /** @brief The App the running Scripting serves. */
        [[nodiscard]] App& GetApp() const noexcept
        {
            return *m_app;
        }

        /** @brief The active scene's entities, as they are now. */
        [[nodiscard]] EntityList& Entities() const
        {
            return m_app->GetActiveScene().GetEntityList();
        }

      private:
        friend class Scripting;
        explicit CallContext(App& app) noexcept : m_app(&app)
        {
        }
        App* m_app;
    };

    namespace Detail
    {
        // The function Iris calls for the native function `Function`: it finds the active call context and calls
        // `Function` with it (and, when `State` is not void, with the extension state after it), or, when no Scripting
        // is running, returns the "not running" value without calling it.
        template <auto Function, typename State, typename R, typename... Args>
        struct NativeFunctionCall
        {
            using Result = R;
            // The extension state it takes, or void for none.
            using StateType = State;

            struct NoValue
            {
            };
            // What C# gets when no Scripting is running. Written only by Scripting::Install, when it registers the
            // native function, before .NET can call this.
            static inline std::conditional_t<std::is_void_v<R>, NoValue, R> notRunning{};

            // The extension state of the extension that lists it, written beside `notRunning`. Unused when `State` is
            // void.
            static inline State* state = nullptr;

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
                if constexpr (std::is_void_v<State>)
                {
                    return Function(*context, arguments...);
                }
                else
                {
                    return Function(*context, *state, arguments...);
                }
            }
        };

        // NativeFunctionCall for a C++ function taking the call context first, and optionally a reference to its
        // extension state (a non-const class type) second, noexcept or not. Matched on the pointer's type, not the
        // pointer: MSVC lets a noexcept pointer match both value specializations.
        template <auto Function, typename Pointer = decltype(Function)>
        struct NativeFunctionTraits;
        template <auto Function, typename R, typename... Args>
        struct NativeFunctionTraits<Function, R (*)(const CallContext&, Args...)>
            : NativeFunctionCall<Function, void, R, Args...>
        {
        };
        template <auto Function, typename R, typename... Args>
        struct NativeFunctionTraits<Function, R (*)(const CallContext&, Args...) noexcept>
            : NativeFunctionCall<Function, void, R, Args...>
        {
        };
        template <auto Function, typename R, typename State, typename... Args>
            requires(std::is_class_v<State> && !std::is_const_v<State>)
        struct NativeFunctionTraits<Function, R (*)(const CallContext&, State&, Args...)>
            : NativeFunctionCall<Function, State, R, Args...>
        {
        };
        template <auto Function, typename R, typename State, typename... Args>
            requires(std::is_class_v<State> && !std::is_const_v<State>)
        struct NativeFunctionTraits<Function, R (*)(const CallContext&, State&, Args...) noexcept>
            : NativeFunctionCall<Function, State, R, Args...>
        {
        };

        // The type a native function's extension state must have, if it takes one.
        template <typename State>
        [[nodiscard]] std::optional<std::type_index> StateTypeOf() noexcept
        {
            if constexpr (std::is_void_v<State>)
            {
                return std::nullopt;
            }
            else
            {
                return std::type_index(typeid(State));
            }
        }
    } // namespace Detail

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
     * `bool`, `char16_t`, pointers, a shared struct or component by pointer) and, as parameters, `std::string_view`. A
     * C++ exception that escapes it is stopped and logged by Iris.
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
            return NativeFunction(std::move(name), Detail::StateTypeOf<typename Traits::StateType>(),
                                  [](std::string_view bindingName, void* state) {
                                      if constexpr (!std::is_void_v<typename Traits::Result>)
                                      {
                                          Traits::notRunning = {};
                                      }
                                      Traits::state = static_cast<typename Traits::StateType*>(state);
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
            return NativeFunction(std::move(name), Detail::StateTypeOf<typename Traits::StateType>(),
                                  [whenNotRunning](std::string_view bindingName, void* state) {
                                      Traits::notRunning = whenNotRunning;
                                      Traits::state = static_cast<typename Traits::StateType*>(state);
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
        using Registrar = std::function<Iris::CallResult<void>(std::string_view bindingName, void* state)>;

        NativeFunction(std::string name, std::optional<std::type_index> stateType, Registrar registrar)
            : m_name(std::move(name)), m_stateType(stateType), m_register(std::move(registrar))
        {
        }

        // Sets the "not running" value and the extension state (`state`, of the type StateType() names, or null for a
        // stateless function), and registers the function Iris calls under BindingName().
        [[nodiscard]] Iris::CallResult<void> Register(void* state) const
        {
            return m_register(BindingName(), state);
        }

        // The type of extension state it takes, or none for a stateless native function.
        [[nodiscard]] const std::optional<std::type_index>& StateType() const noexcept
        {
            return m_stateType;
        }

        std::string m_name;
        std::optional<std::type_index> m_stateType;
        Registrar m_register;
    };

    /**
     * @brief What one engine package, or the app, gives scripting: its native functions, its components, events and
     * shared structs, its extension state, and the shared assemblies that are its C# contract. Passed to
     * Scripting::Install in ScriptingConfig::extensions; scripting knows nothing of the package beyond this.
     */
    struct ScriptingExtension
    {
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
         * @brief Its C# contract: assemblies every script load shares as one copy, after the scripting contract (which
         * the built-in extension shares) and in extension order. A relative path is in the bridge directory, beside the
         * scripting contract, where a package's deploy function puts its contract.
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

      private:
        friend class Scripting;
        std::shared_ptr<void> m_state;
        std::optional<std::type_index> m_stateType;
    };
} // namespace Fenrir
