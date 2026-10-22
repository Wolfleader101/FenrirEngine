#pragma once

#include <filesystem>
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

    // What the engine's native functions act on. Iris calls a native function through a plain function pointer with no
    // user data, so the running Scripting's context is process-wide state: one Scripting is active at a time, and every
    // script call happens on the main thread (the thread that runs the App's sequential systems).
    struct EngineContext
    {
        App* app = nullptr;
        ComponentRegistry* components = nullptr;

        // What every native function receives while this context is active.
        std::optional<CallContext> call;

        // The structural changes scripts have made since they were last played back.
        ChangeBuffer* changes = nullptr;

        // How many times a script has called into the engine to run a query (diagnostic: a query makes a fixed number).
        std::size_t queryCalls = 0;

        // Set only while a script assembly reports the components it defines (while it loads): where to put them. Null
        // at any other time, so a stray call from a script is ignored.
        std::vector<ScriptComponentDescription>* discovering = nullptr;
        std::vector<ScriptEventDescription>* discoveringEvents = nullptr;
    };

    // Scripting's way to make the CallContext its native functions receive, and for the built-in ones to reach the rest
    // of the state they act on.
    struct CallContextAccess
    {
        [[nodiscard]] static CallContext Make(App& app, EngineContext& engine) noexcept
        {
            return CallContext(app, engine);
        }

        [[nodiscard]] static EngineContext& Engine(const CallContext& context) noexcept
        {
            return *context.m_engine;
        }
    };

    // Makes `context` the active one. False (and nothing changes) if another is already active.
    [[nodiscard]] bool ActivateEngineContext(EngineContext* context) noexcept;

    // Clears the active context if it is `context`.
    void DeactivateEngineContext(EngineContext* context) noexcept;

    /**
     * @brief What the engine itself gives scripting, as the extension every Scripting includes first: its native
     * functions (Log, Time, Entity, components, events, queries, changes, resources;
     * "Fenrir.Scripting.Native.<Method>", the methods of the scripting contract's Native class), its components
     * (EngineComponents), and as its shared assemblies `math`, the path of the Fenrir.Math contract, then `contract`,
     * the path of the scripting contract, which is built on it (ADR-0029).
     */
    [[nodiscard]] ScriptingExtension BuiltInExtension(const std::filesystem::path& math,
                                                      const std::filesystem::path& contract);
} // namespace Fenrir::Detail
