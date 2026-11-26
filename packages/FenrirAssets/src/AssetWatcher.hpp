#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirCore/ReloadWatcher.hpp"

namespace Fenrir
{
    class ILogger;
} // namespace Fenrir

namespace Fenrir::Detail
{
    /** @brief What a file on disk looks like now (its modification time and size); none when it cannot be looked at. */
    [[nodiscard]] std::optional<ReloadWatcher::Stamp> StampOf(const std::filesystem::path& file);

    /**
     * @brief The store's watch over the files it loaded (ADR-0081 §1): one ReloadWatcher per file, polled between
     * frames, their looks spread across the interval. It decides when a file changed and has settled; the store reloads
     * it. A file is watched under its key, the absolute path the store keys its assets by, with the kind of asset it
     * holds, so a kind with its own way of reloading (ticket 09's imported files) is told apart from a data asset.
     */
    class AssetWatcher
    {
      public:
        explicit AssetWatcher(ReloadWatcher::Settings settings) : m_settings(settings)
        {
        }

        /**
         * @brief Watches the file `key` names as `stamp` (what it looked like when it was read) at `now`, or, for a
         * file watched already (one the store just saved), takes `stamp` as what is loaded, so the store's own write is
         * no change. `name` is the file's asset path, which the deleted-file warning names it by.
         */
        void Track(const std::string& key, std::string name, AssetKind kind, std::optional<ReloadWatcher::Stamp> stamp,
                   ReloadWatcher::Clock::time_point now);

        /** @brief Stops watching the file `key` names: the store holds nothing from it any more. */
        void Forget(const std::string& key);

        /**
         * @brief Looks at the files due a look at `now`. A file that has gone is warned about once and keeps its data;
         * the next file there is a change. Each changed, settled file is taken as loaded as it is now (so a reload that
         * fails is tried again only when the file changes again) and named to `reload`, once this poll has looked at
         * every file, by its key and kind. `reload` may load files, which are watched, or free them, which are not.
         */
        void Poll(ReloadWatcher::Clock::time_point now, ILogger& logger,
                  const std::function<void(const std::string& key, AssetKind kind)>& reload);

      private:
        struct WatchedFile
        {
            std::filesystem::path path;
            std::string name;
            AssetKind kind = AssetKind::Data;
            ReloadWatcher watcher;
            // Set while the file cannot be looked at, so its going is warned about once.
            bool missing = false;
        };

        ReloadWatcher::Settings m_settings;
        // Ordered by key, so each poll looks at the files, and reloads them, in the same order.
        std::map<std::string, WatchedFile> m_files;
        // How many files have been watched, which spreads each new file's looks across the interval.
        std::uint32_t m_watched = 0;
    };
} // namespace Fenrir::Detail
