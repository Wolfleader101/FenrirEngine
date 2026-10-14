#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <Iris/Iris.hpp>

#include "FenrirApp/App.hpp"
#include "FenrirScripting/ComponentRegistry.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"

namespace Fenrir
{
    /**
     * @brief Where scripting finds .NET and the project's script assembly.
     */
    struct ScriptingConfig
    {
        /**
         * @brief The project's one script assembly (the `scriptAssembly` field of the .feproj). Empty means the
         * project has no scripts: Install then does nothing and never starts .NET.
         */
        std::filesystem::path scriptAssembly;

        /**
         * @brief The directory holding Iris.Bridge.dll, its .runtimeconfig.json and the scripting contract
         * Fenrir.Scripting.dll (the build's fenrir_deploy_scripting puts them next to the executable). Empty means
         * the executable's own directory.
         */
        std::filesystem::path bridgeDirectory;

        /**
         * @brief An absolute .NET installation root, or empty for the default search.
         */
        std::filesystem::path dotnetRoot;

        /**
         * @brief Everything scripting knows besides the engine's own API: what engine packages give scripting (native
         * functions, native components, C++ events and their C# contracts), and what the app gives, as an extension of
         * its own. A package left out here is simply not scriptable. The components and events are registered before
         * the script assembly loads and checked against it as it does; events only C# defines need nothing here. Two
         * extensions declaring one native function, component or event name fail Install.
         */
        std::vector<ScriptingExtension> extensions;

        /**
         * @brief Whether to watch `scriptAssembly` and reload it, between frames, when a build replaces it. Either way
         * the assembly is loaded from a private copy, so a build can always overwrite the original.
         */
        bool hotReload = true;

        /**
         * @brief Where the per-generation copies of the script assembly go. Empty means a directory of its own under
         * the system temporary directory. Each loaded generation has a subdirectory, deleted once that generation is
         * unloaded.
         */
        std::filesystem::path reloadDirectory;

        /** @brief How often the watcher looks at the file. Zero looks every frame. */
        std::chrono::milliseconds reloadInterval{250};

        /**
         * @brief How long the file must stay unchanged, and openable, before it counts as a finished build.
         */
        std::chrono::milliseconds reloadDebounce{400};

        /**
         * @brief How long a reload waits for the old generation to unload. A generation that has not unloaded by then
         * is kept track of (see Scripting::PendingUnloadCount) and the reload goes on.
         */
        std::chrono::milliseconds unloadTimeout{2000};
    };

    struct ScriptingProbe;

    /**
     * @brief Owns the .NET runtime and the loaded script assembly for one App.
     *
     * Scripting::Install starts the Iris Host, routes Iris's log messages to the App's logger, and loads the
     * script assembly. Nothing here throws and nothing is fatal: a missing or invalid assembly, or a missing
     * runtime, is logged and the App keeps running without scripts.
     *
     * Everything Iris owns is released by Shutdown, which runs when the App finishes Run (in its Exit phase)
     * and again, harmlessly, when this object is destroyed. Destroy it before the App and before `main`
     * returns, never during static destruction: releasing calls into the runtime.
     *
     * All calls happen on the thread that runs the App's sequential systems. Not thread-safe. Only one Scripting
     * is active in a process at a time: the engine's native functions act on that one App, so a second Install
     * while another is active logs an error and runs without scripts.
     */
    class Scripting
    {
      public:
        /**
         * @brief Starts .NET and loads the script assembly for `app`. The returned object must outlive
         * `app.Run()`, and be destroyed before `app`.
         */
        [[nodiscard]] static Scripting Install(App& app, ScriptingConfig config);

        ~Scripting();
        Scripting(Scripting&&) noexcept;
        Scripting& operator=(Scripting&&) noexcept;
        Scripting(const Scripting&) = delete;
        Scripting& operator=(const Scripting&) = delete;

        /**
         * @brief Whether the .NET runtime is running (Install started it and Shutdown has not run).
         */
        [[nodiscard]] bool IsRunning() const noexcept;

        /**
         * @brief Whether the script assembly loaded.
         */
        [[nodiscard]] bool HasScripts() const noexcept;

        /**
         * @brief The component registered under a name (native, or defined by the loaded script assembly), or null.
         * Pass its `type` to EntityList's Get, Has, Add, Remove and Count to reach the component on an entity from
         * native code, whatever defines it.
         */
        [[nodiscard]] const ComponentRegistration* FindComponent(std::string_view name) const;

        /**
         * @brief Reloads the script assembly now, from the file `scriptAssembly` names, instead of waiting for the
         * watcher. The order is the one the watcher uses: the old systems' OnDestroy, release and unload the old
         * generation, load the new one all-or-nothing, and if that fails load the previous generation again. Call it
         * between frames, not from a script. Returns success if the new generation is running; a failure names why, and
         * the previous generation is running again if it could be restored (see HasScripts).
         */
        Iris::CallResult<void> Reload();

        /** @brief How many generations of the script assembly have been loaded (successfully) since Install. */
        [[nodiscard]] int Generation() const noexcept;

        /**
         * @brief How many released generations have not unloaded yet. Normally zero after a reload; a generation stays
         * here when something still holds a type from it, and its copy of the assembly stays on disk until it is
         * collected.
         */
        [[nodiscard]] std::size_t PendingUnloadCount() const noexcept;

        /**
         * @brief Releases the script assembly and the runtime host, and stops forwarding Iris's log messages.
         * Idempotent.
         */
        void Shutdown() noexcept;

      private:
        /** @brief The scripting package's tests look inside through this one probe; nothing else does. */
        friend struct ScriptingProbe;

        struct State;

        /**
         * @brief What the tests read: how many times the engine called into the script's systems (once per phase that
         * has systems, per frame, and per fixed step for Tick), how many times scripts called into the engine to run
         * queries, and how many changes are recorded and not yet played back.
         */
        struct Diagnostics
        {
            std::size_t phaseCalls = 0;
            std::size_t queryCalls = 0;
            std::size_t pendingChanges = 0;
        };

        /** @brief The diagnostic counters; zeros when there is no state. */
        [[nodiscard]] Diagnostics ReadDiagnostics() const noexcept;

        /** @brief What ApplyChanges did. */
        struct ChangeSummary
        {
            std::size_t applied = 0;
            /** Changes that named an entity destroyed earlier in the same batch: not applied, and logged as warnings.
             */
            std::size_t skipped = 0;
        };

        /**
         * @brief Plays back the structural changes scripts have made since the last call, in the order they were made.
         * The system runner does this from C# after each system (Native.ChangesApply); native code never needs to, so
         * only the tests call it, to stand in for the runner. Nothing when there is no state.
         *
         * A script that creates, destroys, adds or removes (Entity.Create, Destroy, Add, Remove) does not touch the ECS
         * storage at the time: the change is recorded, so every Ref<T> the script holds stays valid until it returns.
         * Reads (Get, Has, IsValid) are never recorded and see the state as it is now. A created entity's id is usable
         * at once; it gets its Transform, Name and Relationship at playback. Playback skips, with a warning, a change
         * naming an entity an earlier change destroyed.
         */
        ChangeSummary ApplyChanges();

        /**
         * @brief Drops the recorded changes instead of playing them back. Entities they created are destroyed. Reads
         * are unaffected.
         */
        void DiscardChanges();

        /** @brief The loaded script assembly, or null when there are no scripts. Valid until Shutdown. */
        [[nodiscard]] Iris::Component* LoadedComponent() noexcept;

        explicit Scripting(std::shared_ptr<State> state) noexcept;
        [[nodiscard]] static Scripting InstallImpl(App& app, ScriptingConfig config);
        static void ReportFailure(App& app, const char* what) noexcept;
        std::shared_ptr<State> m_state;
    };
} // namespace Fenrir
