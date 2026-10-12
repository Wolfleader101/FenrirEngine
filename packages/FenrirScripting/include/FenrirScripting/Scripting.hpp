#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include <Iris/Iris.hpp>

#include "FenrirApp/App.hpp"

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
         * @brief Further assemblies every script load shares as one copy. The Fenrir.Scripting contract is always
         * shared, from `bridgeDirectory`, so it does not belong here.
         */
        std::vector<std::filesystem::path> sharedAssemblies;
    };

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
         * @brief The loaded script assembly, or null when there are no scripts. The pointer stays valid until
         * Shutdown. This is the hook the runner and the tests call into scripts through.
         */
        [[nodiscard]] Iris::Component* GetComponent() noexcept;

        /**
         * @brief Releases the script assembly and the runtime host, and stops forwarding Iris's log messages.
         * Idempotent.
         */
        void Shutdown() noexcept;

      private:
        struct State;
        explicit Scripting(std::shared_ptr<State> state) noexcept;
        [[nodiscard]] static Scripting InstallImpl(App& app, ScriptingConfig config);
        static void ReportFailure(App& app, const char* what) noexcept;
        std::shared_ptr<State> m_state;
    };
} // namespace Fenrir
