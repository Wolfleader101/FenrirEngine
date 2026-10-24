#pragma once

#include <cstddef>
#include <filesystem>
#include <span>

#include "Importers/ImageImporter.hpp"

namespace Fenrir::Detail
{
    /** @brief Whether `file` is a cubemap file, by its `.cubemap` extension (any case). */
    [[nodiscard]] bool IsCubemapFile(const std::filesystem::path& file);

    /**
     * @brief Reads a `.cubemap` file, whose bytes are `bytes`, and the six face images it names into one cubemap image
     * (ImageShape::Cubemap, faces +X, -X, +Y, -Y, +Z, -Z).
     *
     * The file is text, one face a line: the face's name (`+X`, `-X`, `+Y`, `-Y`, `+Z` or `-Z`), white space, and its
     * image's path relative to the file. Lines may come in any order; blank lines and lines starting with `#` are
     * skipped. Every face must be named once, and the faces must be square images of one size, format and colour
     * space. Gives the importer's message naming the face or file at fault otherwise.
     */
    [[nodiscard]] ImageImport ImportCubemapFile(const std::filesystem::path& file, std::span<const std::byte> bytes);
} // namespace Fenrir::Detail
