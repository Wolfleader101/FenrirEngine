#pragma once

#include <Iris/Iris.hpp>

#include "ComponentRegistry.hpp"
#include "FenrirApp/App.hpp"
#include "ScriptComponents.hpp"

namespace Fenrir::Detail
{
    // What the engine's internal calls act on. An internal call is a plain function pointer with no user data, so
    // the App it serves is process-wide state: one Scripting is active at a time, and every script call happens on
    // the main thread (the thread that runs the App's sequential systems).
    struct EngineContext
    {
        App* app = nullptr;
        ComponentRegistry* components = nullptr;

        // Set only while a script assembly reports the components it defines (while it loads): where to put them. Null at any
        // other time, so a stray call from a script is ignored.
        std::vector<ScriptComponentDescription>* discovering = nullptr;
    };

    // Makes `context` the active one. False (and nothing changes) if another is already active.
    [[nodiscard]] bool ActivateEngineContext(EngineContext* context) noexcept;

    // Clears the active context if it is `context`.
    void DeactivateEngineContext(EngineContext* context) noexcept;

    // Registers every engine internal call with Iris (idempotent), under the names the Fenrir.Scripting contract
    // binds: "Fenrir.Scripting.Native.<Field>". Must happen before the Host shares the contract.
    [[nodiscard]] Iris::CallResult<void> RegisterEngineBindings();
} // namespace Fenrir::Detail
