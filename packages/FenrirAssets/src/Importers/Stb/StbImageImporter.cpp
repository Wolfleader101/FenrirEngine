#include "Importers/ImageImporter.hpp"

#include <climits>
#include <cstring>
#include <memory>

#include <stb_image.h>

namespace Fenrir::Detail
{
    namespace
    {
        // stb_image hands back memory it allocated, freed with its own call.
        struct StbFree
        {
            void operator()(void* pixels) const noexcept
            {
                stbi_image_free(pixels);
            }
        };

        std::string StbError()
        {
            const char* reason = stbi_failure_reason();
            return reason != nullptr ? std::string(reason) : std::string("stb_image could not read the file");
        }
    } // namespace

    ImageImport ImportImage(std::span<const std::byte> file)
    {
        if (file.size() > static_cast<std::size_t>(INT_MAX))
        {
            return {std::nullopt, "the file is too large for stb_image (over 2 GiB)"};
        }
        const auto* bytes = reinterpret_cast<const stbi_uc*>(file.data());
        const int length = static_cast<int>(file.size());

        int width = 0;
        int height = 0;
        int channels = 0;
        const bool hdr = stbi_is_hdr_from_memory(bytes, length) != 0;

        ImageData image;
        std::size_t pixelBytes = 0;
        std::unique_ptr<void, StbFree> pixels;
        if (hdr)
        {
            pixels.reset(stbi_loadf_from_memory(bytes, length, &width, &height, &channels, STBI_rgb_alpha));
            image.format = ImageFormat::RGBA32F;
            image.colourSpace = ColourSpace::Linear;
            pixelBytes = 4 * sizeof(float);
        }
        else
        {
            pixels.reset(stbi_load_from_memory(bytes, length, &width, &height, &channels, STBI_rgb_alpha));
            image.format = ImageFormat::RGBA8;
            image.colourSpace = ColourSpace::Srgb;
            pixelBytes = 4;
        }
        if (pixels == nullptr)
        {
            return {std::nullopt, StbError()};
        }

        image.width = static_cast<std::uint32_t>(width);
        image.height = static_cast<std::uint32_t>(height);
        image.pixels.resize(std::size_t{image.width} * image.height * pixelBytes);
        std::memcpy(image.pixels.data(), pixels.get(), image.pixels.size());
        return {std::move(image), {}};
    }
} // namespace Fenrir::Detail
