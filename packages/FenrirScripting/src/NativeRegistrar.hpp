#pragma once

#include <Iris/Iris.hpp>

#include "FenrirScripting/ScriptingExtension.hpp"

namespace Fenrir::Detail
{
    /**
     * @brief Where an extension set registers its native functions: each under its binding name, with its extension's
     * state (null for a stateless one). Two adapters stand behind it: one over Iris, in Scripting.cpp, which is the one
     * line that reaches Iris's registration; and a fake in the rules tests, which records every registration and
     * refuses the one it is told to, so every rule of the extension set is checked with no .NET.
     */
    class NativeRegistrar
    {
      public:
        NativeRegistrar() = default;
        virtual ~NativeRegistrar() = default;
        NativeRegistrar(const NativeRegistrar&) = delete;
        NativeRegistrar& operator=(const NativeRegistrar&) = delete;

        /** @brief Registers `function` with `state` as the extension state its calls receive. */
        [[nodiscard]] virtual Iris::CallResult<void> Register(const NativeFunction& function, void* state) = 0;
    };
} // namespace Fenrir::Detail
