#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Fenrir
{
    class ILogger;
} // namespace Fenrir

namespace Fenrir::Detail
{
    /**
     * @brief Where the store reads a data asset's envelope from (ADR-0085 §3): the bytes at an asset path, and whether
     * one is there. The directory source over the asset root is the only one now; a cooked build swaps in a pack
     * source that serves the same paths. Private to FenrirAssets: game code loads through the store's doors.
     *
     * An asset path here is root-relative, with forward slashes and its extension, and no `#label`: the file part of
     * an asset path (ADR-0076). Main thread only, as the store's loads are.
     */
    class AssetSource
    {
      public:
        virtual ~AssetSource() = default;

        /** @brief Whether there is a file at `path`. */
        [[nodiscard]] virtual bool Exists(std::string_view path) = 0;

        /** @brief The bytes of the file at `path`; none when there is no file there or it could not be read. */
        [[nodiscard]] virtual std::optional<std::vector<std::byte>> Read(std::string_view path) = 0;
    };

    /**
     * @brief The source reading files under `root` (DirectorySource.cpp). Paths compare case-sensitively; in a
     * development build, a path that matches a file only when case is ignored finds that file and warns to `logger`,
     * once per path, so a project authored on a case-insensitive file system still loads where case matters.
     * `logger` must outlive it.
     */
    [[nodiscard]] std::unique_ptr<AssetSource> MakeDirectorySource(std::filesystem::path root, ILogger& logger);

    /**
     * @brief Why `path`, an asset path written in a file, is refused, or empty when it is not (ADR-0076 §1, ADR-0085
     * §1): it is absolute, or it climbs through `..`. Only the part before a `#label` is checked; a `builtin:` name and
     * a bare `#label` pass.
     */
    [[nodiscard]] std::string FilePathProblem(std::string_view path);
} // namespace Fenrir::Detail
