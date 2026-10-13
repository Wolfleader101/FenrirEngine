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

#include "FenrirLogger/ILogger.hpp"

namespace Fenrir::Detail
{
    // A directory holding one generation's private copy of the script assembly (see GenerationLifecycle.cpp).
    struct ScriptCopy;

    // The life of the script assembly's generations: each one a private copy of the file loaded into its own Iris
    // component, replaced by the next build with the previous one restored if the next fails, released, and waited for
    // until the runtime has collected it. It also decides when the file on disk is ready to be loaded again.
    //
    // It knows nothing of what a generation contains. The host supplies that through hooks.
    class GenerationLifecycle
    {
      public:
        using Result = Iris::CallResult<void>;

        struct Settings
        {
            // The build the project names. It is only ever copied; the copy is what loads.
            std::filesystem::path assembly;
            // Where the per-generation copies go. Removed at Release if nothing else is in it.
            std::filesystem::path scratchRoot;
            bool hotReload = true;
            std::chrono::milliseconds reloadInterval{250};
            std::chrono::milliseconds reloadDebounce{400};
            std::chrono::milliseconds unloadTimeout{2000};
        };

        struct Hooks
        {
            // All-or-nothing checks on a freshly loaded component. Nothing may change if this fails.
            std::function<Result(const Iris::Component&)> completeLoad;
            // Starts what runs inside the loaded generation (its systems). A failure releases the generation again.
            std::function<Result(const Iris::Component&)> start;
            // Stops what ran inside the generation, before it is released, and forgets what it defined.
            std::function<void()> stop;
            // Runs before a reload takes the old generation down, so nothing a script recorded is left half done.
            std::function<void()> beforeReload;
        };

        // What the watcher makes of the file at one moment.
        enum class Watch
        {
            Idle,     // The file is the one that is loaded, or cannot be looked at.
            Settling, // It changed, and a build may still be writing it.
            Ready,    // It changed, has stayed the same for the debounce, and can be opened.
        };

        // Not usable until Configure. Until then every operation does nothing and reports nothing loaded.
        GenerationLifecycle() = default;
        GenerationLifecycle(const GenerationLifecycle&) = delete;
        GenerationLifecycle& operator=(const GenerationLifecycle&) = delete;

        void Configure(Iris::Host* host, ILogger* logger, Settings settings, Hooks hooks);

        // The first generation, from the file as it is now.
        [[nodiscard]] Result Load();

        // Replaces the running generation with a new load of the file: the hook `beforeReload`, then the old
        // generation's `stop`, release, a wait for it to unload, the new generation all-or-nothing, and if that fails
        // the previous generation again. Success means the new generation is running; a failure names why, and the
        // previous generation is running again if it could be restored. Refuses (NotInitialized) when nothing is
        // configured, and when a reload is already under way.
        [[nodiscard]] Result Reload();

        // What the watcher decides at `now`. A change must be seen twice, unchanged, and for the debounce, before it is
        // Ready. Looks at the file at most once per reloadInterval; in between it reports Idle.
        [[nodiscard]] Watch Poll(std::chrono::steady_clock::time_point now);

        // Poll, and Reload when the file is Ready. Nothing escapes: a throw is logged.
        void ReloadIfReady(std::chrono::steady_clock::time_point now) noexcept;

        // Releases the loaded generation (after `stop`). Its copy is kept for a restore.
        void Unload();

        // Waits up to the unload timeout for each released generation, deleting its copy once it is collected.
        void Sweep();

        // Gives everything back: the retired generations, the copies, the scratch directory if it is empty.
        void Release() noexcept;

        [[nodiscard]] bool HasGeneration() const noexcept
        {
            return m_component.has_value();
        }
        [[nodiscard]] Iris::Component* Component() noexcept
        {
            return m_component.has_value() ? &*m_component : nullptr;
        }
        [[nodiscard]] int Generation() const noexcept
        {
            return m_generation;
        }
        [[nodiscard]] std::size_t PendingUnloadCount() const noexcept
        {
            return m_retired.size();
        }
        [[nodiscard]] bool Configured() const noexcept
        {
            return m_host != nullptr;
        }

      private:
        struct FileStamp
        {
            std::filesystem::file_time_type modified{};
            std::uintmax_t size = 0;
            bool operator==(const FileStamp&) const = default;
        };

        [[nodiscard]] std::optional<FileStamp> StampNow() const;
        [[nodiscard]] Result LoadGeneration(std::shared_ptr<ScriptCopy> existing);
        void Retire(Iris::Component&& released, std::shared_ptr<ScriptCopy> copy);

        Iris::Host* m_host = nullptr;
        ILogger* m_logger = nullptr;
        Settings m_settings;
        Hooks m_hooks;

        std::optional<Iris::Component> m_component;
        int m_generation = 0;
        int m_copyCounter = 0;
        std::shared_ptr<ScriptCopy> m_activeCopy;
        std::shared_ptr<ScriptCopy> m_previousCopy; // Kept until the next generation loads, to restore if it does not.

        // A released generation waits here until the runtime confirms it was collected.
        struct Retired
        {
            Iris::Component component;
            std::shared_ptr<ScriptCopy> copy;
            int generation = 0;
            bool warned = false;
        };
        std::vector<Retired> m_retired;

        // The watcher.
        std::optional<FileStamp> m_loadedStamp;
        std::optional<FileStamp> m_pendingStamp;
        std::chrono::steady_clock::time_point m_pendingSince{};
        std::chrono::steady_clock::time_point m_lastPoll{};
        bool m_reloading = false;
    };
} // namespace Fenrir::Detail
