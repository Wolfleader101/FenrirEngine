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
        std::filesystem::path Utf8Path(std::string_view text)
        {
            return std::filesystem::path(
                std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
        }

        std::string Utf8Text(const std::filesystem::path& path)
        {
            const std::u8string text = path.generic_u8string();
            return {reinterpret_cast<const char*>(text.data()), text.size()};
        }

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
            // The file `path` names, or none. In a development build, each name of the path is matched against its
            // directory's entries, exactly if one is spelled so, else ignoring case with a warning once per path.
            std::optional<std::filesystem::path> Locate(std::string_view path)
            {
#ifdef NDEBUG
                const std::filesystem::path file = m_root / Utf8Path(path);
                std::error_code error;
                return std::filesystem::is_regular_file(file, error) ? std::optional(file) : std::nullopt;
#else
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
                    const std::filesystem::path relative = current.lexically_relative(m_root);
                    m_logger->Warn(
                        "Asset path '{}' matches the file '{}' only when case is ignored: asset paths compare "
                        "case-sensitively, so where file names do too it names no file",
                        path, Utf8Text(relative));
                }
                return current;
#endif
            }

#ifndef NDEBUG
            // The entry of `directory` named `name`: the one spelled exactly so, else one spelled so ignoring case,
            // which clears `exact`; none when neither is there.
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
                    if (!folded && Folded(found) == Folded(wanted))
                    {
                        folded = entry.path();
                    }
                }
                if (folded)
                {
                    exact = false;
                }
                return folded;
            }
#endif

            std::filesystem::path m_root;
            ILogger* m_logger;
#ifndef NDEBUG
            // The paths already warned about, so each is warned once.
            std::unordered_set<std::string> m_warned;
#endif
        };

        // Whether `part`, one name of a path, is a drive (`C:`), as a Windows path's first is.
        bool IsDrive(std::string_view part)
        {
            return part.size() == 2 && part[1] == ':' && std::isalpha(static_cast<unsigned char>(part[0])) != 0;
        }
    } // namespace

    std::unique_ptr<AssetSource> MakeDirectorySource(std::filesystem::path root, ILogger& logger)
    {
        return std::make_unique<DirectorySource>(std::move(root), logger);
    }

    std::string FilePathProblem(std::string_view path)
    {
        if (path.starts_with("builtin:"))
        {
            return {};
        }
        path = path.substr(0, path.find('#'));
        // Either slash counts, so a path written on Windows is checked as it would be read there.
        if (path.starts_with('/') || path.starts_with('\\'))
        {
            return "is absolute: a path in a file is relative to the asset root";
        }
        std::size_t start = 0;
        while (start <= path.size())
        {
            const std::size_t end = std::min(path.find_first_of("/\\", start), path.size());
            const std::string_view part = path.substr(start, end - start);
            if (start == 0 && IsDrive(part))
            {
                return "is absolute: a path in a file is relative to the asset root";
            }
            if (part == "..")
            {
                return "climbs through `..`: a path in a file stays under the asset root";
            }
            start = end + 1;
        }
        return {};
    }
} // namespace Fenrir::Detail
