#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "Device/GraphicsDevice.hpp"
#include "FenrirAssets/ImageData.hpp"

namespace Fenrir::Render
{
    /** @brief How many mip levels a full chain for an image of `width` x `height` has, down to 1 x 1. */
    [[nodiscard]] std::uint32_t MipCount(std::uint32_t width, std::uint32_t height);

    /** @brief The device format an image is uploaded as: its own format, sRGB-decoded on sampling if it is sRGB. */
    [[nodiscard]] Graphics::TextureFormat TextureFormatOf(const ImageData& image);

    /** @brief Reads and writes one texel of an image format as four linear floats, for making mips. */
    struct TexelCodec
    {
        /** @brief The size of one texel. */
        std::size_t bytes = 4;
        /** @brief One texel's four channels, as linear values. */
        void (*read)(std::span<const std::byte> texel, std::array<float, 4>& out) = nullptr;
        /** @brief Four linear values, as one texel. */
        void (*write)(const std::array<float, 4>& in, std::span<std::byte> texel) = nullptr;
    };

    /** @brief The codec of an image's pixels: colours are averaged as linear values, so sRGB mips keep brightness. */
    [[nodiscard]] TexelCodec CodecOf(const ImageData& image);

    /**
     * @brief Writes one layer of `texture` (a cube face, else 0) from `pixels`, `width` x `height`, and every mip level
     * below it, each made from the one above (ADR-0024: mips are made at upload).
     */
    void WriteWithMips(Graphics::GraphicsDevice& device, Graphics::TextureHandle texture, std::uint32_t layer,
                       std::uint32_t mipLevels, std::span<const std::byte> pixels, std::uint32_t width,
                       std::uint32_t height, const TexelCodec& codec);
} // namespace Fenrir::Render
