#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <Iris/Iris.hpp>

#include "ComponentLoader.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "ReloadWatcher.hpp"

namespace Fenrir::Detail
{
    /** @brief A directory holding one generation's private copy of the script assembly (see GenerationLifecycle.cpp).
     */
    struct ScriptCopy;

    /**
     * @brief The life of the script assembly's generations: each one a private copy of the file loaded through a
     * ComponentLoader, replaced by the next build with the previous one restored if the next fails, released, and
     * waited for until the loader says it has been collected. It reads the file on disk, and a ReloadWatcher decides
     * from that when it is ready to be loaded again.
     *
     * It knows nothing of what a generation contains: the host supplies that through Hooks. It is set up in two steps,
     * because the host that owns it exists before the settings are known; until Configure every operation does nothing
     * and reports nothing loaded.
     */
    class GenerationLifecycle
    {
      public:
        using Result = Iris::CallResult<void>;

        /** @brief Where the script assembly is, and how to watch and unload it. */
        struct Settings
        {
            /** @brief The build the project names. It is only ever copied; the copy is what loads. */
            std::filesystem::path assembly;
            /** @brief Where the per-generation copies go. Removed at Release if nothing else is in it. */
            std::filesystem::path scratchRoot;
            /** @brief Whether ReloadIfReady reloads at all. */
            bool hotReload = true;
            /** @brief How often the watcher looks at the file. Zero looks every call. */
            std::chrono::milliseconds reloadInterval{250};
            /** @brief How long the file must stay unchanged before it counts as a finished build. */
            std::chrono::milliseconds reloadDebounce{400};
            /** @brief How long a reload waits for the old generation to unload. */
            std::chrono::milliseconds unloadTimeout{2000};
        };

        /** @brief What the host does at the points where a generation comes and goes. */
        struct Hooks
        {
            /** @brief All-or-nothing checks on a freshly loaded component. Nothing may change if this fails. */
            std::function<Result(LoadedComponent&)> reconcile;
            /** @brief Starts what runs inside the loaded generation (its systems). A failure releases it again. */
            std::function<Result(LoadedComponent&)> start;
            /** @brief Stops what ran inside the generation, before it is released, and forgets what it defined. */
            std::function<void()> stop;
            /** @brief Runs before a reload takes the old generation down, so nothing a script recorded is left half
             * done. */
            std::function<void()> beforeReload;
        };

        /** @brief What the watcher makes of the file at one moment (see ReloadWatcher). */
        using Watch = ReloadWatcher::Watch;

        GenerationLifecycle() = default;
        GenerationLifecycle(const GenerationLifecycle&) = delete;
        GenerationLifecycle& operator=(const GenerationLifecycle&) = delete;

        /** @brief Gives the lifecycle what it needs. `loader` and `logger` are borrowed and must outlive it. */
        void Configure(ComponentLoader& loader, ILogger* logger, Settings settings, Hooks hooks);

        /** @brief Loads the first generation, from the file as it is now. NotInitialized before Configure. */
        [[nodiscard]] Result Load();

        /**
         * @brief Replaces the running generation with a new load of the file: the hook `beforeReload`, then the old
         * generation's `stop`, release, a wait for it to unload, the new generation all-or-nothing, and if that fails
         * the previous generation again.
         *
         * @return success if the new generation is running; otherwise why not (the previous generation is running
         * again if it could be restored). NotInitialized when nothing is configured, and when a reload is already under
         * way.
         */
        [[nodiscard]] Result Reload();

        /**
         * @brief What the watcher decides at `now`. A change must be seen twice, unchanged, and for the debounce,
         * before it is Ready. Looks at the file at most once per reloadInterval; in between it reports Idle.
         */
        [[nodiscard]] Watch Poll(std::chrono::steady_clock::time_point now);

        /** @brief Poll, and Reload when the file is Ready. Nothing escapes: a throw is logged. */
        void ReloadIfReady(std::chrono::steady_clock::time_point now) noexcept;

        /** @brief Releases the loaded generation (after `stop`). Its copy is kept for a restore. */
        void Unload();

        /** @brief Waits up to the unload timeout for each released generation, deleting its copy once it is collected.
         */
        void Sweep();

        /** @brief Gives everything back: the retired generations, the copies, the scratch directory if it is empty. */
        void Release() noexcept;

        /** @brief Whether a generation is loaded. */
        [[nodiscard]] bool HasGeneration() const noexcept
        {
            return m_component != nullptr;
        }

        /** @brief The loaded generation, or null. Valid until it is unloaded. */
        [[nodiscard]] LoadedComponent* Component() noexcept
        {
            return m_component.get();
        }

        /** @brief How many generations have been loaded (successfully) since the first. */
        [[nodiscard]] int Generation() const noexcept
        {
            return m_generation;
        }

        /** @brief How many released generations have not been collected yet. */
        [[nodiscard]] std::size_t PendingUnloadCount() const noexcept
        {
            return m_retired.size();
        }

      private:
        using FileStamp = ReloadWatcher::Stamp;

        [[nodiscard]] std::optional<FileStamp> StampNow() const;
        [[nodiscard]] Result LoadGeneration(std::shared_ptr<ScriptCopy> existing);
        void Retire(std::unique_ptr<LoadedComponent> released, std::shared_ptr<ScriptCopy> copy);

        ComponentLoader* m_loader = nullptr;
        ILogger* m_logger = nullptr;
        Settings m_settings;
        Hooks m_hooks;

        std::unique_ptr<LoadedComponent> m_component;
        int m_generation = 0;
        int m_copyCounter = 0;
        std::shared_ptr<ScriptCopy> m_activeCopy;
        std::shared_ptr<ScriptCopy> m_previousCopy; // Kept until the next generation loads, to restore if it does not.

        // A released generation waits here until the loader confirms it was collected.
        struct Retired
        {
            std::unique_ptr<LoadedComponent> component;
            std::shared_ptr<ScriptCopy> copy;
            int generation = 0;
            bool warned = false;
        };
        std::vector<Retired> m_retired;

        // Decides when the file on disk is a finished build; this class reads the file and supplies what it asks for.
        ReloadWatcher m_watcher;
        bool m_reloading = false;
    };
} // namespace Fenrir::Detail
