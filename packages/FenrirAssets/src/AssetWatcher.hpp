#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirCore/ReloadWatcher.hpp"

namespace Fenrir
{
    class ILogger;
    class Scene;
} // namespace Fenrir

namespace Fenrir::Detail
{
    struct AssetStoreState;
    /** @brief What a file on disk looks like now (its modification time and size); none when it cannot be looked at. */
    [[nodiscard]] std::optional<ReloadWatcher::Stamp> StampOf(const std::filesystem::path& file);

    /**
     * @brief The store's watch over the files it loaded (ADR-0081 §1): one ReloadWatcher per file, polled between
     * frames, their looks spread across the interval. It decides when a file changed and has settled; the store reloads
     * it. A file is watched under its key, the absolute path the store keys its assets by, with the kind of asset it
     * holds, so each kind reloads its own way: a data asset through its envelope, an imported file through its
     * importer. A scene file LoadScene filled a scene from is watched apart, under its key alone, outside the asset
     * kinds: it holds no asset, and a change to it patches the scenes filled from it (ADR-0081 §3).
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

        /** @brief Whether the file `key` names is watched. */
        [[nodiscard]] bool Watches(const std::string& key) const
        {
            return m_files.contains(key);
        }

        /**
         * @brief Watches the scene file `key` names, as Track watches an asset's file: `stamp` is what it looked like
         * when it was read, and a scene file watched already takes `stamp` as what is loaded. `name` is its asset path.
         */
        void TrackScene(const std::string& key, std::string name, std::optional<ReloadWatcher::Stamp> stamp,
                        ReloadWatcher::Clock::time_point now);

        /** @brief Stops watching the scene file `key` names: no scene is filled from it any more. */
        void ForgetScene(const std::string& key);

        /** @brief Whether the scene file `key` names is watched. */
        [[nodiscard]] bool WatchesScene(const std::string& key) const
        {
            return m_scenes.contains(key);
        }

        /**
         * @brief Looks at the files due a look at `now`. A file that has gone is warned about once and keeps its data;
         * the next file there is a change. Each changed, settled file is taken as loaded as it is now (so a reload that
         * fails is tried again only when the file changes again) and named to `reload`, once this poll has looked at
         * every file, by its key and kind. `reload` may load files, which are watched, or free them, which are not.
         * Then each changed, settled scene file is named to `patch` by its key, after every asset file's reload, so a
         * scene patched reads the assets just reloaded.
         */
        void Poll(ReloadWatcher::Clock::time_point now, ILogger& logger,
                  const std::function<void(const std::string& key, AssetKind kind)>& reload,
                  const std::function<void(const std::string& key)>& patch);

      private:
        struct WatchedFile
        {
            std::filesystem::path path;
            std::string name;
            // The kind of asset it holds; unused for a scene file.
            AssetKind kind = AssetKind::Data;
            ReloadWatcher watcher;
            // Set while the file cannot be looked at, so its going is warned about once.
            bool missing = false;
        };

        // Starts watching `key` in `files` as Track describes, or marks a watched one loaded at `stamp`.
        void Start(std::map<std::string, WatchedFile>& files, const std::string& key, std::string name, AssetKind kind,
                   std::optional<ReloadWatcher::Stamp> stamp, ReloadWatcher::Clock::time_point now);

        // Looks at `file` if it is due a look at `now`, warning once when it has gone; whether it changed and settled.
        [[nodiscard]] static bool Look(WatchedFile& file, ReloadWatcher::Clock::time_point now, ILogger& logger);

        ReloadWatcher::Settings m_settings;
        // Ordered by key, so each poll looks at the files, and reloads them, in the same order.
        std::map<std::string, WatchedFile> m_files;
        // The scene files, by key, ordered so as the asset files are.
        std::map<std::string, WatchedFile> m_scenes;
        // How many files have been watched, which spreads each new file's looks across the interval.
        std::uint32_t m_watched = 0;
    };

    /**
     * @brief Records that LoadScene filled `scene` from the scene file `fileKey` (its asset path `assetPath`, its
     * blocks' ids `fileIds`), with a SceneFileRecord on the scene, and, with the store's watch on, watches the file as
     * it was at `stamp`, looked at before it was read (ADR-0081 §3).
     */
    void RecordSceneFile(const std::shared_ptr<AssetStoreState>& state, Scene& scene, const std::string& fileKey,
                         const std::string& assetPath, std::unordered_set<std::uint64_t> fileIds,
                         std::optional<ReloadWatcher::Stamp> stamp);
} // namespace Fenrir::Detail
