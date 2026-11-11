#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirAssets/ImageData.hpp"
#include "FenrirPainter/Paint.hpp"

namespace Fenrir
{
    /**
     * @brief The rows that gradients of more than two stops read their colours from (ADR-0060): an image `Width`
     * texels wide with one row per gradient, each texel a colour along the gradient, sRGB-encoded and premultiplied,
     * stored linear so that sampling interpolates the values as the vertex's two stops do.
     *
     * The painter keeps nothing between frames, so this is the caller's: one per UI root, kept across frames. Each
     * frame the painter adds the rows its gradients need (equal stops share a row), and before the list is drawn the
     * caller writes them into the image, `store.Replace(stops.GetImage(), stops.TakeImageData())`, which also empties
     * it for the next frame. A gradient that finds it full draws with its first and last stops.
     */
    class StopTexture
    {
      public:
        /** @brief Texels in a row. */
        static constexpr std::uint32_t Width = 256;

        /** @brief An image of `rows` transparent rows, to add to the asset store as this texture's image. */
        [[nodiscard]] static ImageData Blank(std::uint32_t rows);

        /** @brief Rows of `image`, which holds `rows` of them (made from Blank). */
        explicit StopTexture(AssetHandle<Image> image, std::uint32_t rows = 64);

        [[nodiscard]] AssetHandle<Image> GetImage() const noexcept
        {
            return m_image;
        }

        /** @brief How many rows this frame has used. */
        [[nodiscard]] std::uint32_t RowCount() const noexcept
        {
            return static_cast<std::uint32_t>(m_rows.size());
        }

        /**
         * @brief The row for `stops` (sorted by offset), added unless an equal one is there: its centre's V
         * coordinate, or none when every row is used.
         */
        [[nodiscard]] std::optional<float> AddRow(std::span<const GradientStop> stops);

        /** @brief This frame's rows as the whole image, unused rows transparent; the texture is then empty. */
        [[nodiscard]] ImageData TakeImageData();

      private:
        struct Row
        {
            std::vector<GradientStop> stops;
            std::vector<std::byte> texels;
        };

        AssetHandle<Image> m_image;
        std::uint32_t m_capacity = 0;
        std::vector<Row> m_rows;
    };
} // namespace Fenrir
