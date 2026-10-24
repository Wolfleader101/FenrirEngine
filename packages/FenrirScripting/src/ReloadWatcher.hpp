#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>

namespace Fenrir::Detail
{
    /**
     * @brief Decides when a changed script assembly counts as a finished build: a change must be seen, then stay the
     * same for a debounce, and the file must be openable (a build still writing it, or a scanner holding it, says no).
     *
     * It does no file access and keeps no clock: it is told the time, and given the two questions it may ask of the
     * file, which it asks only when the answer matters (a look at most once per interval; an open only once a build has
     * settled).
     */
    class ReloadWatcher
    {
      public:
        using Clock = std::chrono::steady_clock;

        struct Settings
        {
            /** @brief Whether the watcher decides anything at all. Off, it is always Idle and never looks. */
            bool hotReload = true;
            /** @brief How often the file is looked at. Zero looks at every Poll. */
            std::chrono::milliseconds interval{250};
            /** @brief How long a changed file must stay the same before it counts as a finished build. */
            std::chrono::milliseconds debounce{400};
        };

        /** @brief What a file looked like at one moment: when it was last written and how big it was. */
        struct Stamp
        {
            std::filesystem::file_time_type modified{};
            std::uintmax_t size = 0;
            bool operator==(const Stamp&) const = default;
        };

        /** @brief What the watcher makes of the file at one moment. */
        enum class Watch
        {
            /** The file is the one that is loaded, or cannot be looked at, or it is not time to look. */
            Idle,
            /** It changed, and a build may still be writing it. */
            Settling,
            /** It changed, has stayed the same for the debounce, and can be opened. */
            Ready,
        };

        ReloadWatcher() = default;
        explicit ReloadWatcher(Settings settings) : m_settings(settings)
        {
        }

        /**
         * @brief Records what the file looked like when the generation now loaded was loaded (nothing if it could not
         * be looked at), and forgets any change that was waiting: a build loaded by hand must not be loaded a second
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
         * @param lookAtFile what the file looks like now, or nothing if it cannot be looked at; called at most once per
         * interval
         * @param canBeOpened whether the file can be opened for reading; called only for a build that has stayed the
         * same for the debounce
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
} // namespace Fenrir::Detail
