#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
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

    // An address or count a native function writes into C#'s memory: a native-sized unsigned integer, which C# holds as
    // nuint (api/FenrirScripting.binding.toml maps this alias).
    using NativeAddress = std::uintptr_t;

    // What the engine's native functions act on: the built-in extension's extension state. Iris calls a native function
    // through a plain function pointer with no user data, so which Scripting is running is process-wide state (the
    // active EngineContext, whose call context every native function receives): one Scripting is active at a time, and
    // every script call happens on the main thread (the thread that runs the App's sequential systems).
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

    // Makes `context` the active one. False (and nothing changes) if another is already active.
    [[nodiscard]] bool ActivateEngineContext(EngineContext* context) noexcept;

    // Clears the active context if it is `context`.
    void DeactivateEngineContext(EngineContext* context) noexcept;

    /**
     * @brief What the engine itself gives scripting, as the extension every Scripting includes first: its native
     * functions (Log, Time, Entity, components, events, queries, changes; "Fenrir.Scripting.Native.<Method>", the
     * methods of the scripting contract's Native class), its components (EngineComponents), and `contract`, the path of
     * the scripting contract, as its shared assembly. `engine` is its extension state, which every one of its native
     * functions takes.
     */
    [[nodiscard]] ScriptingExtension BuiltInExtension(const std::filesystem::path& contract,
                                                      std::shared_ptr<EngineContext> engine);
} // namespace Fenrir::Detail
