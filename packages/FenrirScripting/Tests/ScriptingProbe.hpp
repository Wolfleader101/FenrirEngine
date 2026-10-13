#pragma once

#include <cstddef>

#include "FenrirScripting/Scripting.hpp"

namespace Fenrir
{
    /**
     * @brief The tests' one way inside Scripting: its diagnostic counters and the loaded Iris component. Scripting
     * names this type as a friend, so none of it is part of the interface the engine sees.
     */
    struct ScriptingProbe
    {
        /** @brief How many times the engine called into the script's systems since Install. */
        [[nodiscard]] static std::size_t PhaseCalls(const Scripting& scripting)
        {
            return scripting.ReadDiagnostics().phaseCalls;
        }

        /** @brief How many times scripts called into the engine to run queries since Install. */
        [[nodiscard]] static std::size_t QueryCalls(const Scripting& scripting)
        {
            return scripting.ReadDiagnostics().queryCalls;
        }

        /** @brief How many changes are recorded and not yet played back. */
        [[nodiscard]] static std::size_t PendingChanges(const Scripting& scripting)
        {
            return scripting.ReadDiagnostics().pendingChanges;
        }

        /** @brief The loaded script assembly, or null when there are no scripts: the hook the tests call scripts
         * through. */
        [[nodiscard]] static Iris::Component* Component(Scripting& scripting)
        {
            return scripting.LoadedComponent();
        }
    };
} // namespace Fenrir
