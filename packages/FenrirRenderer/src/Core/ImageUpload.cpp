#include "Core/ImageUpload.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include <glm/gtc/packing.hpp>

namespace Fenrir::Render
{
    namespace
    {
        using namespace Fenrir::Graphics;

        /** @brief sRGB-encoded bytes to linear values, by byte. */
        const std::array<float, 256>& SrgbToLinear()
        {
            static const std::array<float, 256> table = [] {
                std::array<float, 256> made{};
                for (std::size_t i = 0; i < made.size(); ++i)
                {
                    const float c = static_cast<float>(i) / 255.0f;
                    made.at(i) = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
                }
                return made;
            }();
            return table;
        }

        /** @brief Linear values, in 4096 steps over [0, 1], to sRGB-encoded bytes. */
        const std::array<std::uint8_t, 4096>& LinearToSrgb()
        {
            static const std::array<std::uint8_t, 4096> table = [] {
                std::array<std::uint8_t, 4096> made{};
                for (std::size_t i = 0; i < made.size(); ++i)
                {
                    const float l = static_cast<float>(i) / 4095.0f;
                    const float c = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
                    made.at(i) = static_cast<std::uint8_t>(std::lround(std::clamp(c, 0.0f, 1.0f) * 255.0f));
                }
                return made;
            }();
            return table;
        }

        std::uint8_t ToByte(float value)
        {
            return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
        }

        /**
         * @brief The next mip level of a `width` x `height` level: each texel the average of the 2 x 2 texels above it
         * (an odd last row or column is left out, and a side of 1 stays 1).
         */
        std::vector<std::byte> Downsample(std::span<const std::byte> level, std::uint32_t width, std::uint32_t height,
                                          const TexelCodec& codec)
        {
            const std::uint32_t nextWidth = std::max(1u, width / 2);
            const std::uint32_t nextHeight = std::max(1u, height / 2);
            std::vector<std::byte> next(std::size_t{nextWidth} * nextHeight * codec.bytes);
            std::array<float, 4> texel{};
            for (std::uint32_t y = 0; y < nextHeight; ++y)
            {
                for (std::uint32_t x = 0; x < nextWidth; ++x)
                {
                    std::array<float, 4> sum{};
                    for (const std::uint32_t sy : {std::min(2 * y, height - 1), std::min(2 * y + 1, height - 1)})
                    {
                        for (const std::uint32_t sx : {std::min(2 * x, width - 1), std::min(2 * x + 1, width - 1)})
                        {
                            codec.read(level.subspan((std::size_t{sy} * width + sx) * codec.bytes, codec.bytes), texel);
                            for (std::size_t c = 0; c < 4; ++c)
                            {
                                sum.at(c) += texel.at(c);
                            }
                        }
                    }
                    for (float& channel : sum)
                    {
                        channel *= 0.25f;
                    }
                    codec.write(sum,
                                std::span(next).subspan((std::size_t{y} * nextWidth + x) * codec.bytes, codec.bytes));
                }
            }
            return next;
        }
    } // namespace

    std::uint32_t MipCount(std::uint32_t width, std::uint32_t height)
    {
        std::uint32_t levels = 1;
        for (std::uint32_t side = std::max(width, height); side > 1; side /= 2)
        {
            ++levels;
        }
        return levels;
    }

    TextureFormat TextureFormatOf(const ImageData& image)
    {
        if (image.format == ImageFormat::RGBA16F)
        {
            return TextureFormat::Rgba16Float;
        }
        if (image.format == ImageFormat::RGBA32F)
        {
            return TextureFormat::Rgba32Float;
        }
        if (image.format == ImageFormat::R8)
        {
            return TextureFormat::R8Unorm;
        }
        return image.colourSpace == ColourSpace::Srgb ? TextureFormat::Rgba8Srgb : TextureFormat::Rgba8Unorm;
    }

    TexelCodec CodecOf(const ImageData& image)
    {
        TexelCodec codec;
        codec.bytes = BytesPerPixel(image.format);
        if (image.format == ImageFormat::R8)
        {
            // One channel, as FenrirText's glyph coverage: red, the rest left at none and alpha at one.
            codec.read = [](std::span<const std::byte> texel, std::array<float, 4>& out) {
                out = {static_cast<float>(texel[0]) / 255.0f, 0.0f, 0.0f, 1.0f};
            };
            codec.write = [](const std::array<float, 4>& in, std::span<std::byte> texel) {
                texel[0] = std::byte{ToByte(in[0])};
            };
        }
        else if (image.format == ImageFormat::RGBA32F)
        {
            codec.read = [](std::span<const std::byte> texel, std::array<float, 4>& out) {
                std::memcpy(out.data(), texel.data(), sizeof(float) * 4);
            };
            codec.write = [](const std::array<float, 4>& in, std::span<std::byte> texel) {
                std::memcpy(texel.data(), in.data(), sizeof(float) * 4);
            };
        }
        else if (image.format == ImageFormat::RGBA16F)
        {
            codec.read = [](std::span<const std::byte> texel, std::array<float, 4>& out) {
                std::array<std::uint16_t, 4> halves{};
                std::memcpy(halves.data(), texel.data(), sizeof(halves));
                for (std::size_t c = 0; c < 4; ++c)
                {
                    out.at(c) = glm::unpackHalf1x16(halves.at(c));
                }
            };
            codec.write = [](const std::array<float, 4>& in, std::span<std::byte> texel) {
                std::array<std::uint16_t, 4> halves{};
                for (std::size_t c = 0; c < 4; ++c)
                {
                    halves.at(c) = glm::packHalf1x16(in.at(c));
                }
                std::memcpy(texel.data(), halves.data(), sizeof(halves));
            };
        }
        else if (image.colourSpace == ColourSpace::Srgb)
        {
            codec.read = [](std::span<const std::byte> texel, std::array<float, 4>& out) {
                const std::span<const std::byte> bytes = texel;
                for (std::size_t c = 0; c < 3; ++c)
                {
                    out.at(c) = SrgbToLinear().at(static_cast<std::size_t>(bytes[c]));
                }
                out[3] = static_cast<float>(bytes[3]) / 255.0f;
            };
            codec.write = [](const std::array<float, 4>& in, std::span<std::byte> texel) {
                const std::span<std::byte> bytes = texel;
                for (std::size_t c = 0; c < 3; ++c)
                {
                    const auto step = static_cast<std::size_t>(std::lround(std::clamp(in.at(c), 0.0f, 1.0f) * 4095.0f));
                    bytes[c] = std::byte{LinearToSrgb().at(step)};
                }
                bytes[3] = std::byte{ToByte(in[3])};
            };
        }
        else
        {
            codec.read = [](std::span<const std::byte> texel, std::array<float, 4>& out) {
                const std::span<const std::byte> bytes = texel;
                for (std::size_t c = 0; c < 4; ++c)
                {
                    out.at(c) = static_cast<float>(bytes[c]) / 255.0f;
                }
            };
            codec.write = [](const std::array<float, 4>& in, std::span<std::byte> texel) {
                const std::span<std::byte> bytes = texel;
                for (std::size_t c = 0; c < 4; ++c)
                {
                    bytes[c] = std::byte{ToByte(in.at(c))};
                }
            };
        }
        return codec;
    }

    void WriteWithMips(GraphicsDevice& device, TextureHandle texture, std::uint32_t layer, std::uint32_t mipLevels,
                       std::span<const std::byte> pixels, std::uint32_t width, std::uint32_t height,
                       const TexelCodec& codec)
    {
        std::vector<std::byte> level;
        std::span<const std::byte> current = pixels;
        for (std::uint32_t mip = 0; mip < mipLevels; ++mip)
        {
            if (mip > 0)
            {
                level = Downsample(current, width, height, codec);
                current = level;
                width = std::max(1u, width / 2);
                height = std::max(1u, height / 2);
            }
            device.WriteTexture(texture, mip, layer, current);
        }
    }
} // namespace Fenrir::Render
