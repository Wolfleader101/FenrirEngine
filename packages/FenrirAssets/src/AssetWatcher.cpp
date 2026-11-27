#include "AssetWatcher.hpp"

#include <fstream>
#include <system_error>
#include <utility>

#include "AssetStoreState.hpp"
#include "FenrirLogger/ILogger.hpp"

namespace Fenrir::Detail
{
    namespace
    {
        // How many parts the interval is cut into for spreading files' looks: files watched in a row look in turn.
        constexpr std::uint32_t SpreadParts = 16;
    } // namespace

    std::optional<ReloadWatcher::Stamp> StampOf(const std::filesystem::path& file)
    {
        std::error_code error;
        const auto modified = std::filesystem::last_write_time(file, error);
        if (error)
        {
            return std::nullopt;
        }
        const auto size = std::filesystem::file_size(file, error);
        if (error)
        {
            return std::nullopt;
        }
        return ReloadWatcher::Stamp{modified, size};
    }

    void AssetWatcher::Track(const std::string& key, std::string name, AssetKind kind,
                             std::optional<ReloadWatcher::Stamp> stamp, ReloadWatcher::Clock::time_point now)
    {
        if (const auto watched = m_files.find(key); watched != m_files.end())
        {
            watched->second.watcher.MarkLoaded(stamp);
            watched->second.missing = !stamp.has_value();
            return;
        }
        WatchedFile file{Utf8Path(key), std::move(name), kind, ReloadWatcher(m_settings), !stamp.has_value()};
        file.watcher.MarkLoaded(stamp);
        // A first look now, as though made a part of the interval ago, at the stamp just read rather than the disk: the
        // file's next look is that part sooner than a whole interval, so files loaded together do not all look on the
        // same frame (ADR-0081 §1).
        const auto part = m_settings.interval * (m_watched++ % SpreadParts) / SpreadParts;
        (void)file.watcher.Poll(now - part, [&] { return stamp; }, [] { return true; });
        m_files.emplace(key, std::move(file));
    }

    void AssetWatcher::Forget(const std::string& key)
    {
        m_files.erase(key);
    }

    void AssetWatcher::Poll(ReloadWatcher::Clock::time_point now, ILogger& logger,
                            const std::function<void(const std::string& key, AssetKind kind)>& reload)
    {
        std::vector<std::pair<std::string, AssetKind>> changed;
        for (auto& [key, file] : m_files)
        {
            bool looked = false;
            bool found = false;
            const auto lookAtFile = [&] {
                looked = true;
                std::optional<ReloadWatcher::Stamp> stamp = StampOf(file.path);
                found = stamp.has_value();
                return stamp;
            };
            const auto canBeOpened = [&] { return std::ifstream(file.path, std::ios::binary).is_open(); };
            const ReloadWatcher::Watch watch = file.watcher.Poll(now, lookAtFile, canBeOpened);
            if (looked && !found && !file.missing)
            {
                // A deleted file keeps what it loaded (ADR-0081 §1); whatever is written there next is a change.
                file.missing = true;
                file.watcher.MarkLoaded(std::nullopt);
                logger.Warn("The file '{}' is gone: what it loaded is kept, and it reloads if the file comes back",
                            file.name);
            }
            else if (looked && found)
            {
                file.missing = false;
            }
            if (watch == ReloadWatcher::Watch::Ready)
            {
                // Taken as loaded whether or not the reload succeeds, so a broken file is reported once per change.
                file.watcher.MarkLoaded(StampOf(file.path));
                changed.emplace_back(key, file.kind);
            }
        }
        for (const auto& [key, kind] : changed)
        {
            reload(key, kind);
        }
    }
} // namespace Fenrir::Detail
