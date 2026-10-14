#pragma once

#include <Iris/Iris.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirScripting/ComponentRegistry.hpp"

namespace Fenrir
{
    class CallContext;

    namespace Detail
    {
        struct CallContextAccess;

        // The active Scripting's call context, or null when no Scripting is running.
        [[nodiscard]] const CallContext* ActiveCallContext() noexcept;
    } // namespace Detail

    /**
     * @brief What a native function receives first: the App the running Scripting serves, and through it the active
     * scene's entities. Only scripting makes one, and a native function is only ever called with the one that is
     * active.
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
        friend struct Detail::CallContextAccess;
        explicit CallContext(App& app) noexcept : m_app(&app)
        {
        }
        App* m_app;
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

        // NativeFunctionCall for a C++ function taking the call context first, noexcept or not.
        template <auto Function>
        struct NativeFunctionTraits;
        template <typename R, typename... Args, R (*Function)(const CallContext&, Args...)>
        struct NativeFunctionTraits<Function> : NativeFunctionCall<Function, R, Args...>
        {
        };
        template <typename R, typename... Args, R (*Function)(const CallContext&, Args...) noexcept>
        struct NativeFunctionTraits<Function> : NativeFunctionCall<Function, R, Args...>
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
     * @brief What one engine package, or the app, gives scripting: its native functions, its components and events, and
     * the shared assemblies that are its C# contract. Passed to Scripting::Install in ScriptingConfig::extensions;
     * scripting knows nothing of the package beyond this.
     */
    struct ScriptingExtension
    {
        /** @brief Who it is, for diagnostics. */
        std::string name;

        /** @brief Its C# contract: assemblies shared with every script load, as ScriptingConfig::sharedAssemblies are.
         */
        std::vector<std::filesystem::path> sharedAssemblies;

        /** @brief The native functions its contract calls. Their names must be unique across every extension. */
        std::vector<NativeFunction> functions;

        /** @brief Native components it exposes to C#, as ScriptingConfig::components. */
        std::vector<ComponentRegistration> components;

        /** @brief C++ events it shares with C#, as ScriptingConfig::events. */
        std::vector<EventRegistration> events;
    };
} // namespace Fenrir
