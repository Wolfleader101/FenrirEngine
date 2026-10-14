#pragma once

#include <Iris/Iris.hpp>

#include <optional>

#include "ChangeBuffer.hpp"
#include "ComponentRegistry.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "ScriptComponents.hpp"

namespace Fenrir::Detail
{
    // An event type a script assembly reported while it loaded: a name, a size and an alignment, as for a component.
    using ScriptEventDescription = ScriptComponentDescription;

    // What the engine's internal calls act on. An internal call is a plain function pointer with no user data, so
    // the App it serves is process-wide state: one Scripting is active at a time, and every script call happens on
    // the main thread (the thread that runs the App's sequential systems).
    struct EngineContext
    {
        App* app = nullptr;

        // What every native function receives while this context is active.
        std::optional<CallContext> call;
        ComponentRegistry* components = nullptr;

        // The structural changes scripts have made since they were last played back.
        ChangeBuffer* changes = nullptr;

        // How many times a script has called into the engine to run a query (diagnostic: a query makes a fixed number).
        std::size_t queryCalls = 0;

        // Set only while a script assembly reports the components it defines (while it loads): where to put them. Null
        // at any other time, so a stray call from a script is ignored.
        std::vector<ScriptComponentDescription>* discovering = nullptr;
        std::vector<ScriptEventDescription>* discoveringEvents = nullptr;
    };

    // Scripting's way to make the CallContext its native functions receive.
    struct CallContextAccess
    {
        [[nodiscard]] static CallContext Make(App& app) noexcept
        {
            return CallContext(app);
        }
    };

    // Makes `context` the active one. False (and nothing changes) if another is already active.
    [[nodiscard]] bool ActivateEngineContext(EngineContext* context) noexcept;

    // Clears the active context if it is `context`.
    void DeactivateEngineContext(EngineContext* context) noexcept;

    // Registers every engine internal call with Iris (idempotent), under the names the Fenrir.Scripting contract
    // binds: "Fenrir.Scripting.Native.<Field>". Must happen before the Host shares the contract.
    [[nodiscard]] Iris::CallResult<void> RegisterEngineBindings();
} // namespace Fenrir::Detail
