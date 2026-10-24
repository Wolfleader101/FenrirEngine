#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>

#include "FenrirAssets/ImageData.hpp"

namespace Fenrir::Detail
{
    /** @brief What the image importer made of a file: the image, or the importer's message saying why there is none. */
    struct ImageImport
    {
        std::optional<ImageData> image;
        std::string error;
    };

    /**
     * @brief Reads a PNG, JPEG or HDR file's bytes into an image, rows top to bottom. PNG and JPEG give RGBA8 marked
     * sRGB, HDR gives RGBA32F marked linear; missing channels are filled (alpha with one). The private image importer
     * seam: one implementation, over stb_image, and nothing outside FenrirAssets's sources sees which.
     */
    [[nodiscard]] ImageImport ImportImage(std::span<const std::byte> file);
} // namespace Fenrir::Detail
