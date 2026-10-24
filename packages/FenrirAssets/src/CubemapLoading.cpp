#include "CubemapLoading.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "AssetStoreState.hpp"

namespace Fenrir::Detail
{
    namespace
    {
        // The face names, in the cubemap's face order.
        constexpr std::array<std::string_view, 6> FaceNames{"+X", "-X", "+Y", "-Y", "+Z", "-Z"};

        std::string_view Trim(std::string_view text)
        {
            const auto space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
            while (!text.empty() && space(text.front()))
            {
                text.remove_prefix(1);
            }
            while (!text.empty() && space(text.back()))
            {
                text.remove_suffix(1);
            }
            return text;
        }

        ImageImport Fail(std::string message)
        {
            return {std::nullopt, std::move(message)};
        }
    } // namespace

    bool IsCubemapFile(const std::filesystem::path& file)
    {
        std::string extension = file.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
        return extension == ".cubemap";
    }

    ImageImport ImportCubemapFile(const std::filesystem::path& file, std::span<const std::byte> bytes)
    {
        const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());

        // Each face's image path, by face, from the file's lines.
        std::array<std::optional<std::filesystem::path>, 6> paths;
        std::istringstream lines(text);
        std::string line;
        std::size_t number = 0;
        while (std::getline(lines, line))
        {
            ++number;
            const std::string_view trimmed = Trim(line);
            if (trimmed.empty() || trimmed.front() == '#')
            {
                continue;
            }
            const std::size_t gap = trimmed.find_first_of(" \t");
            const std::string_view name = trimmed.substr(0, gap);
            const std::string_view path =
                gap == std::string_view::npos ? std::string_view{} : Trim(trimmed.substr(gap));
            const auto face = std::find(FaceNames.begin(), FaceNames.end(), name);
            if (face == FaceNames.end())
            {
                return Fail(std::format("line {} starts with '{}', which is not a face (+X, -X, +Y, -Y, +Z or -Z)",
                                        number, name));
            }
            if (path.empty())
            {
                return Fail(std::format("line {} names face {} but no image for it", number, name));
            }
            std::optional<std::filesystem::path>& slot = paths.at(static_cast<std::size_t>(face - FaceNames.begin()));
            if (slot.has_value())
            {
                return Fail(std::format("face {} is named twice", name));
            }
            slot = file.parent_path() / std::filesystem::path(std::u8string(path.begin(), path.end()));
        }

        ImageData cubemap;
        cubemap.shape = ImageShape::Cubemap;
        for (std::size_t face = 0; face < FaceNames.size(); ++face)
        {
            const std::string_view name = FaceNames.at(face);
            if (!paths.at(face).has_value())
            {
                return Fail(std::format("face {} has no image", name));
            }
            const std::filesystem::path& path = *paths.at(face);
            const std::string shown = path.filename().string();
            std::vector<std::byte> faceBytes;
            if (!ReadAssetFile(path, faceBytes))
            {
                return Fail(std::format("face {}'s image '{}' could not be read", name, shown));
            }
            ImageImport imported = ImportImage(faceBytes);
            if (!imported.image)
            {
                return Fail(std::format("face {}'s image '{}' did not import: {}", name, shown, imported.error));
            }
            const ImageData& image = *imported.image;
            if (image.width != image.height)
            {
                return Fail(std::format("face {}'s image '{}' is {} by {} pixels; faces must be square", name, shown,
                                        image.width, image.height));
            }
            if (face == 0)
            {
                cubemap.width = image.width;
                cubemap.height = image.height;
                cubemap.format = image.format;
                cubemap.colourSpace = image.colourSpace;
            }
            else if (image.width != cubemap.width || image.format != cubemap.format ||
                     image.colourSpace != cubemap.colourSpace)
            {
                return Fail(std::format("face {}'s image '{}' is {} pixels square, or of another format, unlike face "
                                        "+X's {}",
                                        name, shown, image.width, cubemap.width));
            }
            cubemap.pixels.insert(cubemap.pixels.end(), image.pixels.begin(), image.pixels.end());
        }
        return {std::move(cubemap), {}};
    }
} // namespace Fenrir::Detail
