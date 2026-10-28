#pragma once

// What a registration needs and nothing else does: the thunk Iris calls for a native function, the traits that
// read its C++ signature, and the bodies of NativeFunction::Make. Included by the generated registrations through the
// defaults file's `[natives] includes` (packages/FenrirScripting/api/FenrirScripting.binding-defaults.toml) and by a
// hand-written maker by hand; everything else includes ScriptingExtension.hpp alone.

#include "FenrirScripting/ScriptingExtension.hpp"

#include <Iris/Iris.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <utility>

namespace Fenrir
{
    namespace Detail
    {
        // What a native function's call context is built from: the running Scripting's App and change buffer. Both null
        // when no Scripting is running.
        struct ActiveScripting
        {
            App* app = nullptr;
            ChangeBuffer* changes = nullptr;
        };

        // The running Scripting's App and change buffer, from which NativeFunctionCall builds each call's context, or
        // nulls when no Scripting is running.
        [[nodiscard]] ActiveScripting ActiveCallContext() noexcept;

        // The function Iris calls for the native function `Function`: it builds the call context from the running
        // Scripting and `name` and calls `Function` with it (and, when `State` is not void, with the extension state
        // after it), or, when no Scripting is running, returns the "not running" value without calling it.
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

            // The type-and-method part of the name it is registered under (NativeFunction::NativeNameFor), written
            // beside `notRunning`: what each call's context carries as CallContext::Native.
            static inline std::string name;

            static R Call(Args... arguments)
            {
                const ActiveScripting active = ActiveCallContext();
                if (active.app == nullptr)
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
                const CallContext context(*active.app, *active.changes, name);
                if constexpr (std::is_void_v<State>)
                {
                    return Function(context, arguments...);
                }
                else
                {
                    return Function(context, *state, arguments...);
                }
            }
        };

        // NativeFunctionCall for a C++ function taking the call context first, and optionally a reference to its
        // extension state (a non-const class type) second, noexcept or not. Matched on the pointer's type, not the
        // pointer: MSVC lets a noexcept pointer match both value specializations. Declared, with `Pointer`'s default,
        // in ScriptingExtension.hpp, where NativeFunction::Make's declaration names its Result.
        template <auto Function, typename Pointer>
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

    template <auto Function>
    NativeFunction NativeFunction::Make(std::string name)
    {
        using Traits = Detail::NativeFunctionTraits<Function>;
        return NativeFunction(std::move(name), Detail::StateTypeOf<typename Traits::StateType>(),
                              [](std::string_view bindingName, std::string_view nativeName, void* state) {
                                  if constexpr (!std::is_void_v<typename Traits::Result>)
                                  {
                                      Traits::notRunning = {};
                                  }
                                  Traits::state = static_cast<typename Traits::StateType*>(state);
                                  Traits::name = std::string(nativeName);
                                  return Iris::RegisterInternalCall<&Traits::Call>(bindingName);
                              });
    }

    template <auto Function>
        requires(!std::is_void_v<typename Detail::NativeFunctionTraits<Function>::Result>)
    NativeFunction NativeFunction::Make(std::string name,
                                        typename Detail::NativeFunctionTraits<Function>::Result whenNotRunning)
    {
        using Traits = Detail::NativeFunctionTraits<Function>;
        return NativeFunction(std::move(name), Detail::StateTypeOf<typename Traits::StateType>(),
                              [whenNotRunning](std::string_view bindingName, std::string_view nativeName, void* state) {
                                  Traits::notRunning = whenNotRunning;
                                  Traits::state = static_cast<typename Traits::StateType*>(state);
                                  Traits::name = std::string(nativeName);
                                  return Iris::RegisterInternalCall<&Traits::Call>(bindingName);
                              });
    }
} // namespace Fenrir
