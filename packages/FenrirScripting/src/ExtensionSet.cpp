#include "ExtensionSet.hpp"

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>

#if __has_include(<cxxabi.h>)
#include <cxxabi.h>
#endif

namespace Fenrir::Detail
{
    namespace
    {
        // A type's name as its source spells it, for a diagnostic. MSVC's type_info already gives it; the Itanium ABI
        // (GCC, and clang off Windows) gives the mangled name, which is demangled here.
        std::string ReadableName(const std::type_index& type)
        {
#if __has_include(<cxxabi.h>)
            int status = 0;
            const std::unique_ptr<char, decltype(&std::free)> demangled(
                abi::__cxa_demangle(type.name(), nullptr, nullptr, &status), &std::free);
            if (status == 0 && demangled != nullptr)
            {
                return demangled.get();
            }
#endif
            return type.name();
        }
    } // namespace

    std::optional<std::string> ExtensionSet::CheckState(const NativeFunction& function,
                                                        const ScriptingExtension& extension)
    {
        const auto& wanted = function.StateType();
        const auto& carried = extension.m_stateType;
        if (!wanted.has_value() || wanted == carried)
        {
            return std::nullopt;
        }
        const std::string start = "the native function '" + function.Name() + "' of extension '" + extension.name +
                                  "' takes an extension state of type '" + ReadableName(*wanted) +
                                  "', but the extension ";
        return carried.has_value() ? start + "carries one of type '" + ReadableName(*carried) + "'"
                                   : start + "carries none";
    }

    Iris::CallResult<ExtensionSet> ExtensionSet::Install(std::vector<ScriptingExtension> extensions,
                                                         NativeRegistrar& registrar)
    {
        using Result = Iris::CallResult<ExtensionSet>;
        const auto refused = [](std::string why) {
            return Result{Iris::CallStatus::InvalidArgument, std::nullopt, std::move(why), false};
        };

        // Every rule first, so a set that fails one registers nothing: registration cannot be undone.
        std::unordered_map<std::string, std::string> declaredBy;
        for (const auto& extension : extensions)
        {
            for (const auto& function : extension.functions)
            {
                if (function.BindingName().empty())
                {
                    return refused("the native function '" + function.Name() + "' of extension '" + extension.name +
                                   "' is not named Namespace.Type.Method");
                }
                if (auto failure = CheckState(function, extension))
                {
                    return refused(std::move(*failure));
                }
                const auto [first, added] = declaredBy.emplace(function.Name(), extension.name);
                if (!added)
                {
                    return refused(first->second == extension.name
                                       ? "the native function '" + function.Name() +
                                             "' is declared twice by extension '" + extension.name + "'"
                                       : "the native function '" + function.Name() +
                                             "' is declared by both extension '" + first->second + "' and extension '" +
                                             extension.name + "'");
                }
            }
        }
        std::unordered_map<std::string, std::string> stageDeclaredBy;
        for (const auto& extension : extensions)
        {
            if (!extension.stages.empty() && (!extension.placeStages || !extension.removeStages))
            {
                return refused("extension '" + extension.name +
                               "' declares stages but does not give both placeStages and removeStages");
            }
            for (const auto& stage : extension.stages)
            {
                if (stage.empty())
                {
                    return refused("extension '" + extension.name + "' declares a stage with no name");
                }
                const auto [first, added] = stageDeclaredBy.emplace(stage, extension.name);
                if (!added)
                {
                    return refused(first->second == extension.name
                                       ? "the stage '" + stage + "' is declared twice by extension '" + extension.name +
                                             "'"
                                       : "the stage '" + stage + "' is declared by both extension '" + first->second +
                                             "' and extension '" + extension.name + "'");
                }
            }
        }

        ExtensionSet set;
        for (auto& extension : extensions)
        {
            for (const auto& function : extension.functions)
            {
                // A stateless function takes no state; CheckState made sure a stateful one gets its type.
                void* const state = function.StateType().has_value() ? extension.m_state.get() : nullptr;
                if (const auto registered = registrar.Register(function, state); !registered.Succeeded())
                {
                    return Result{registered.status, std::nullopt,
                                  "the native function '" + function.Name() + "' of extension '" + extension.name +
                                      "' could not be registered: " + registered.diagnostic,
                                  registered.diagnosticTruncated};
                }
            }
            if (extension.m_state != nullptr)
            {
                set.m_states.push_back(std::move(extension.m_state));
            }
            for (auto& assembly : extension.sharedAssemblies)
            {
                set.m_sharedAssemblies.push_back(std::move(assembly));
            }
            if (!extension.stages.empty())
            {
                set.m_stages.push_back({extension.name, std::move(extension.stages), std::move(extension.placeStages),
                                        std::move(extension.removeStages)});
            }
        }
        return Result{Iris::CallStatus::Success, std::move(set), {}, false};
    }
} // namespace Fenrir::Detail
