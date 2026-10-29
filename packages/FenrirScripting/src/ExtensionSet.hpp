#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Iris/Iris.hpp>

#include "FenrirScripting/ScriptingExtension.hpp"
#include "NativeRegistrar.hpp"

namespace Fenrir::Detail
{
    /**
     * @brief Every scripting extension an Install was given, the built-in first, once its native functions have passed
     * the naming, state and duplicate rules and been registered: it keeps the extension states alive and gives the
     * contracts in extension order. The rules live here: every native function is named Namespace.Type.Method; no name
     * is declared twice, by one extension or by two; a native taking an extension state is listed by an extension
     * carrying a state of its type; every rule is checked before anything is registered; and a set exists only once
     * every native is registered, so the contracts it lists are shared after the natives they bind.
     */
    class ExtensionSet
    {
      public:
        ExtensionSet(ExtensionSet&&) noexcept = default;
        ExtensionSet& operator=(ExtensionSet&&) noexcept = default;

        /**
         * @brief Checks every rule over every extension first, with no side effect, then registers every native in
         * extension order with its extension's state (null for a stateless native).
         *
         * @return the set, or InvalidArgument naming the function and the extension that broke a rule, or the
         * registrar's status when it refused a registration ("the native function 'X' of extension 'Y' could not be
         * registered: ..."). What was registered before a refusal stays registered: Iris cannot unregister.
         */
        [[nodiscard]] static Iris::CallResult<ExtensionSet> Install(std::vector<ScriptingExtension> extensions,
                                                                    NativeRegistrar& registrar);

        /** @brief The shared assemblies as the extensions listed them, in extension order. */
        [[nodiscard]] const std::vector<std::filesystem::path>& SharedAssemblies() const noexcept
        {
            return m_sharedAssemblies;
        }

      private:
        ExtensionSet() = default;

        // Why `function` cannot be listed by `extension`: it takes an extension state the extension does not carry, or
        // one of another type. Nothing for a stateless function.
        [[nodiscard]] static std::optional<std::string> CheckState(const NativeFunction& function,
                                                                   const ScriptingExtension& extension);

        // Every extension's extension state, kept alive as long as the native functions that receive it can be called.
        std::vector<std::shared_ptr<void>> m_states;
        std::vector<std::filesystem::path> m_sharedAssemblies;
    };
} // namespace Fenrir::Detail
