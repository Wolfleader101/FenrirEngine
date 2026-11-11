// The store's region updates of images (ADR-0062): each writes pixels into the image in place and bumps its revision,
// remembering the region, so a copy of the image (the renderer's texture) takes just what changed.
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <vector>

#include "AssetStoreState.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirLogger/ILogger.hpp"

namespace Fenrir
{
    namespace
    {
        // How many region updates an image remembers: a copy further behind takes the whole image again.
        constexpr std::size_t RememberedImageUpdates = 256;
    } // namespace

    bool AssetStore::UpdateImage(AssetHandle<Image> image, ImageRegion region, std::span<const std::byte> pixels)
    {
        Detail::AssetRecord* record =
            EditRecord(Detail::AssetKind::Image, {image.index, image.generation}, "update an image");
        if (record == nullptr)
        {
            return false;
        }
        auto& data = *static_cast<ImageData*>(record->data.get());
        const auto refuse = [&](const std::string& problem) {
            m_state->logger->Error("Could not update an image: {}", problem);
            return false;
        };
        if (data.shape != ImageShape::Flat)
        {
            return refuse("it is a cubemap, and only flat images update by region");
        }
        if (data.pixels.empty())
        {
            return refuse("it is a render target, which has no pixels to update");
        }
        if (region.width == 0 || region.height == 0)
        {
            return refuse(
                std::format("the region is {} by {} pixels, and needs at least one", region.width, region.height));
        }
        // In 64 bits, so a region near the 32-bit limit cannot wrap around into the image.
        if (std::uint64_t{region.x} + region.width > data.width ||
            std::uint64_t{region.y} + region.height > data.height)
        {
            return refuse(std::format("the region {} by {} at ({}, {}) is not inside its {} by {} pixels", region.width,
                                      region.height, region.x, region.y, data.width, data.height));
        }
        const std::size_t pixelBytes = BytesPerPixel(data.format);
        const std::size_t rowBytes = std::size_t{region.width} * pixelBytes;
        if (pixels.size() != rowBytes * region.height)
        {
            return refuse(std::format("it was given {} bytes of pixels where a {} by {} region needs {}", pixels.size(),
                                      region.width, region.height, rowBytes * region.height));
        }
        for (std::uint32_t row = 0; row < region.height; ++row)
        {
            const std::size_t target = ((std::size_t{region.y} + row) * data.width + region.x) * pixelBytes;
            std::memcpy(data.pixels.data() + target, pixels.data() + row * rowBytes, rowBytes);
        }
        ++record->revision;
        record->imageUpdates.emplace_back(record->revision, region);
        if (record->imageUpdates.size() > RememberedImageUpdates)
        {
            record->imageUpdates.pop_front();
        }
        return true;
    }

    std::optional<std::vector<ImageRegion>> AssetStore::ImageUpdatesSince(AssetHandle<Image> image,
                                                                          std::uint32_t revision) const
    {
        const Detail::AssetRecord* record = m_state->Find(Detail::AssetKind::Image, {image.index, image.generation});
        if (record == nullptr || record->data == nullptr || revision > record->revision)
        {
            return std::nullopt;
        }
        if (revision == record->revision)
        {
            return std::vector<ImageRegion>{};
        }
        // The updates cover every revision after `revision` only if the first one remembered comes right after it.
        const auto& updates = record->imageUpdates;
        if (updates.empty() || updates.front().first > revision + 1)
        {
            return std::nullopt;
        }
        std::vector<ImageRegion> regions;
        for (const auto& [made, region] : updates)
        {
            if (made > revision)
            {
                regions.push_back(region);
            }
        }
        return regions;
    }
} // namespace Fenrir
