#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#include "AssetSource.hpp"
#include "AssetStoreState.hpp"
#include "FenrirLogger/ILogger.hpp"

namespace Fenrir::Detail
{
    namespace
    {
#ifndef NDEBUG
        // `text` with its ASCII letters lowered: how a case-insensitive file system compares names, near enough for
        // the warning, which only has to notice a path that will not load where case matters.
        std::string Folded(std::string text)
        {
            std::ranges::transform(text, text.begin(), [](char c) {
                return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            });
            return text;
        }
#endif

        // The files under one directory root, by root-relative asset path.
        class DirectorySource final : public AssetSource
        {
          public:
            DirectorySource(std::filesystem::path root, ILogger& logger) : m_root(std::move(root)), m_logger(&logger)
            {
            }

            bool Exists(std::string_view path) override
            {
                return Locate(path).has_value();
            }

            std::optional<std::vector<std::byte>> Read(std::string_view path) override
            {
                const std::optional<std::filesystem::path> file = Locate(path);
                std::vector<std::byte> bytes;
                if (!file || !ReadAssetFile(*file, bytes))
                {
                    return std::nullopt;
                }
                return bytes;
            }

          private:
            // The file `path` names, or none. Each name of the path is matched against its directory's entries as
            // spelled, so the comparison is case-sensitive whatever the file system's is. In a development build, a
            // name matched only when case is ignored finds its entry too, with a warning once per path.
            std::optional<std::filesystem::path> Locate(std::string_view path)
            {
                std::filesystem::path current = m_root;
                bool exact = true;
                for (const std::filesystem::path& name : Utf8Path(path))
                {
                    std::optional<std::filesystem::path> matched = MatchEntry(current, name, exact);
                    if (!matched)
                    {
                        return std::nullopt;
                    }
                    current = std::move(*matched);
                }
                std::error_code error;
                if (!std::filesystem::is_regular_file(current, error))
                {
                    return std::nullopt;
                }
                if (!exact && m_warned.insert(std::string(path)).second)
                {
                    m_logger->Warn(
                        "Asset path '{}' matches the file '{}' only when case is ignored: asset paths compare "
                        "case-sensitively, so a release build does not find it",
                        path, Utf8Text(current.lexically_relative(m_root)));
                }
                return current;
            }

            // The entry of `directory` named `name`: the one spelled exactly so; else, in a development build, one
            // spelled so ignoring case, which clears `exact`; none when neither is there.
            static std::optional<std::filesystem::path> MatchEntry(const std::filesystem::path& directory,
                                                                   const std::filesystem::path& name, bool& exact)
            {
                const std::string wanted = Utf8Text(name);
                std::optional<std::filesystem::path> folded;
                std::error_code error;
                for (const std::filesystem::directory_entry& entry :
                     std::filesystem::directory_iterator(directory, error))
                {
                    const std::string found = Utf8Text(entry.path().filename());
                    if (found == wanted)
                    {
                        return entry.path();
                    }
#ifndef NDEBUG
                    if (!folded && Folded(found) == Folded(wanted))
                    {
                        folded = entry.path();
                    }
#endif
                }
                if (folded)
                {
                    exact = false;
                }
                return folded;
            }

            std::filesystem::path m_root;
            ILogger* m_logger;
            // The paths already warned about, so each is warned once.
            std::unordered_set<std::string> m_warned;
        };
    } // namespace

    std::unique_ptr<AssetSource> MakeDirectorySource(std::filesystem::path root, ILogger& logger)
    {
        return std::make_unique<DirectorySource>(std::move(root), logger);
    }
} // namespace Fenrir::Detail
