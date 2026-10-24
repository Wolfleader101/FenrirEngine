#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Fenrir
{
    /** @brief How an image's pixels are stored. Every format has four channels: red, green, blue and alpha. */
    enum class ImageFormat : std::uint8_t
    {
        /** @brief One byte per channel. */
        RGBA8,
        /** @brief One half-precision float per channel. */
        RGBA16F,
        /** @brief One float per channel. */
        RGBA32F,
    };

    /** @brief How an image's red, green and blue values are encoded; alpha is always linear. */
    enum class ColourSpace : std::uint8_t
    {
        /** @brief Encoded through the sRGB curve, as colour textures and pictures usually are. */
        Srgb,
        /** @brief Stored as they are: data such as normals and roughness, and HDR images. */
        Linear,
    };

    /** @brief Whether an image is one flat picture or the six faces of a cubemap. */
    enum class ImageShape : std::uint8_t
    {
        Flat,
        /**
         * @brief Six square faces of one size, for a sky: +X, -X, +Y, -Y, +Z, -Z, each laid out as GL and D3D lay out a
         * cubemap's faces (seen from inside, a face's top row is up). Loaded from a `.cubemap` file (AssetStore::Load).
         */
        Cubemap,
    };

    /** @brief The bytes one pixel of `format` takes. */
    [[nodiscard]] constexpr std::size_t BytesPerPixel(ImageFormat format) noexcept
    {
        if (format == ImageFormat::RGBA32F)
        {
            return 16;
        }
        return format == ImageFormat::RGBA16F ? 8 : 4;
    }

    /**
     * @brief An image as plain CPU-side data (ADR-0021): its size, format, colour space and pixels. It has no mips:
     * those are the renderer's.
     *
     * Pixels are optional. An image with none is a render target, which a camera draws into and materials sample
     * (ADR-0026). With pixels it holds exactly `width * height` of them per face, rows top to bottom, and a cubemap's
     * faces one after another. The store checks this when the image is added or replaced, and refuses an image that
     * breaks it.
     */
    struct ImageData
    {
        /** @brief Width in pixels, at least one. */
        std::uint32_t width = 0;
        /** @brief Height in pixels, at least one; equal to the width in a cubemap. */
        std::uint32_t height = 0;
        /** @brief How the pixels are stored. A render target is RGBA8. */
        ImageFormat format = ImageFormat::RGBA8;
        /**
         * @brief How the colours are encoded. Set by whoever makes the image; an image loaded from a file is sRGB
         * unless it is HDR, which is linear, until its first use says otherwise. A render target is sRGB.
         */
        ColourSpace colourSpace = ColourSpace::Srgb;
        /** @brief One picture, or six cubemap faces. */
        ImageShape shape = ImageShape::Flat;
        /** @brief The pixels, `BytesPerPixel(format)` bytes each; empty for a render target. */
        std::vector<std::byte> pixels;
    };
} // namespace Fenrir
