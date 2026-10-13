#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <Iris/Iris.hpp>

#include "FenrirApp/App.hpp"
#include "FenrirScripting/ComponentRegistry.hpp"

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
         * @brief Native components to expose to C# besides the engine's own (Transform, Name and Relationship), built with
         * MakeComponent. They are registered before the script assembly loads and checked against it as it does.
         */
        std::vector<ComponentRegistration> components;

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
         * @brief Exposes another native component to C# (see MakeComponent and MakeReadOnlyComponent). If a script assembly
         * is loaded, the component's C# struct is checked against its layout straight away: a mismatch is
         * `InvalidArgument` with a diagnostic naming the field, is logged, and releases the script assembly, exactly as a
         * mismatch at load does (all or nothing). A name that is empty or already taken is `InvalidArgument`.
         */
        [[nodiscard]] Iris::CallResult<void> RegisterComponent(ComponentRegistration registration);

        /** @brief MakeComponent<T, Mapping>(name), registered. */
        template <typename T, typename Mapping = T>
        [[nodiscard]] Iris::CallResult<void> RegisterComponent(std::string name)
        {
            return RegisterComponent(MakeComponent<T, Mapping>(std::move(name)));
        }

        /** @brief MakeReadOnlyComponent<T, Mapping>(name), registered. */
        template <typename T, typename Mapping = T>
        [[nodiscard]] Iris::CallResult<void> RegisterReadOnlyComponent(std::string name)
        {
            return RegisterComponent(MakeReadOnlyComponent<T, Mapping>(std::move(name)));
        }

        /**
         * @brief The component registered under a name (native, or defined by the loaded script assembly), or null. Use its
         * operations to add, remove and reach a component on an entity from native code, whatever defines it.
         */
        [[nodiscard]] const ComponentRegistration* FindComponent(std::string_view name) const;

        /** @brief What ApplyChanges did. */
        struct ChangeSummary
        {
            std::size_t applied = 0;
            /** Changes that named an entity destroyed earlier in the same batch: not applied, and logged as warnings. */
            std::size_t skipped = 0;
        };

        /**
         * @brief Plays back the structural changes scripts have made since the last call, in the order they were made.
         *
         * A script that creates, destroys, adds or removes (Entity.Create, Destroy, Add, Remove) does not touch the ECS storage
         * at the time: the change is recorded, so every Ref<T> the script holds stays valid until it returns. Whatever calls
         * into a script (the runner after each system, or `OnCreate`) calls this when the script returns. Reads (Get, Has,
         * IsValid) are never recorded and see the state as it is now, which does not include changes not yet played back.
         * A created entity's id is usable at once; it gets its Transform, Name and Relationship here.
         *
         * Playback skips, with a warning, a create, add or remove naming an entity an earlier change destroyed, so a
         * destroy-then-add on one entity leaves it destroyed. A second destroy of an entity is not an error and is not
         * reported.
         */
        ChangeSummary ApplyChanges();

        /** @brief How many changes are recorded and not yet played back. */
        [[nodiscard]] std::size_t PendingChangeCount() const noexcept;

        /**
         * @brief Drops the recorded changes instead of playing them back (a script threw, say). Entities it created are
         * destroyed. Reads are unaffected.
         */
        void DiscardChanges();

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
