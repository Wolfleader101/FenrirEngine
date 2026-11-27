#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>

namespace Fenrir
{
    /**
     * @brief Decides when a changed file (a rebuilt script assembly, an edited data asset) is finished being written:
     * a change must be seen, then stay the same for a debounce, and the file must be openable (a build or an editor
     * still writing it, or a scanner holding it, says no). Hot reload uses one per watched file (ADR-0081).
     *
     * It does no file access and keeps no clock: it is told the time, and given the two questions it may ask of the
     * file, which it asks only when the answer matters (a look at most once per interval; an open only once a change
     * has settled).
     */
    class ReloadWatcher
    {
      public:
        /** @brief The clock whose time the caller passes to Poll. The watcher never reads it. */
        using Clock = std::chrono::steady_clock;

        /** @brief Whether to watch, how often to look, and how long a change must settle. */
        struct Settings
        {
            /** @brief Whether the watcher decides anything at all. Off, it is always Idle and never looks. */
            bool hotReload = true;
            /** @brief How often the file is looked at. Zero looks at every Poll. */
            std::chrono::milliseconds interval{250};
            /** @brief How long a changed file must stay the same before it counts as finished. */
            std::chrono::milliseconds debounce{400};
        };

        /** @brief What a file looked like at one moment: when it was last written and how big it was. */
        struct Stamp
        {
            /** @brief When the file was last written. */
            std::filesystem::file_time_type modified{};
            /** @brief The file's size in bytes. */
            std::uintmax_t size = 0;
            /** @brief Two stamps are the same file contents as far as the watcher can tell. */
            bool operator==(const Stamp&) const = default;
        };

        /** @brief What the watcher makes of the file at one moment. */
        enum class Watch
        {
            /** @brief The file is the one that is loaded, or cannot be looked at, or it is not time to look. */
            Idle,
            /** @brief It changed, and something may still be writing it. */
            Settling,
            /** @brief It changed, has stayed the same for the debounce, and can be opened. */
            Ready,
        };

        /** @brief A watcher with the default Settings: on, looking every 250 ms, settling for 400 ms. */
        ReloadWatcher() = default;

        /** @brief A watcher with the given settings; nothing is loaded yet, so the first file it sees is a change. */
        explicit ReloadWatcher(Settings settings) : m_settings(settings)
        {
        }

        /**
         * @brief Records what the file looked like when what is now loaded was loaded (nothing if it could not be
         * looked at), and forgets any change that was waiting: a file loaded by hand must not be loaded a second
         * time.
         */
        void MarkLoaded(std::optional<Stamp> stamp)
        {
            m_loaded = stamp;
            m_pending.reset();
        }

        /**
         * @brief What the watcher decides at `now`. A change must be seen twice, unchanged, and for the debounce,
         * before it is Ready.
         *
         * @param now the time, on Clock; the first look needs `now` at least the interval past the clock's start
         * @param lookAtFile what the file looks like now, or nothing if it cannot be looked at; called at most once per
         * interval
         * @param canBeOpened whether the file can be opened for reading; called only for a change that has stayed
         * the same for the debounce
         */
        [[nodiscard]] Watch Poll(Clock::time_point now, const std::function<std::optional<Stamp>()>& lookAtFile,
                                 const std::function<bool()>& canBeOpened);

      private:
        Settings m_settings;
        std::optional<Stamp> m_loaded;
        std::optional<Stamp> m_pending;
        Clock::time_point m_pendingSince{};
        Clock::time_point m_lastLook{};
    };
} // namespace Fenrir
