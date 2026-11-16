#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirAssets/AssetStore.hpp"

namespace Fenrir
{
    /** @brief How a glyph atlas sizes its pages. */
    struct GlyphAtlasConfig
    {
        /** @brief A new atlas's first page is this many pixels square. */
        std::uint32_t initialSize = 256;
        /**
         * @brief A full page doubles its side until it is this many pixels square; past that the atlas opens another
         * page of this size.
         */
        std::uint32_t maxSize = 2048;
        /** @brief Empty pixels kept right of and below each rectangle, so sampling one never reads its neighbour. */
        std::uint32_t padding = 1;
    };

    /** @brief Where an atlas put a rectangle: its page, and the region on that page, padding excluded. */
    struct AtlasRegion
    {
        /** @brief The page, an index into GlyphAtlas::Pages. */
        std::uint32_t page = 0;
        /** @brief The rectangle on the page, in pixels. */
        ImageRegion region;

        friend constexpr bool operator==(const AtlasRegion&, const AtlasRegion&) = default;
    };

    namespace Detail
    {
        struct GlyphAtlasState;
    }

    /**
     * @brief A dynamic glyph atlas (ADR-0062): pages of R8 coverage, each an `Image` asset the atlas owns, filled with
     * rectangles on demand by a skyline packer (bottom-left fit). Each rectangle is uploaded as it is added, through
     * AssetStore::UpdateImage, so the image's updates name exactly the rectangles added. A full page doubles its side
     * (AssetStore::Replace, keeping what it held) up to the configured limit, and then the atlas opens a new page; only
     * the newest page takes rectangles. FenrirText keeps one per bitmap face and pixel size, and one per SDF font that
     * every size shares (its pages then hold distance fields, not coverage); it is public so that its packing can be
     * tested and so that other coverage (icons) can be packed alike.
     *
     * Main thread only, as the asset store is. The store must outlive the atlas, which unloads its pages when it ends
     * or is assigned another's.
     */
    class GlyphAtlas
    {
      public:
        /** @brief An atlas with no pages yet, keeping its pages in `store`; the first Add opens one. */
        GlyphAtlas(AssetStore& store, GlyphAtlasConfig config = {});
        ~GlyphAtlas();
        GlyphAtlas(const GlyphAtlas&) = delete;
        GlyphAtlas& operator=(const GlyphAtlas&) = delete;
        GlyphAtlas(GlyphAtlas&&) noexcept;
        GlyphAtlas& operator=(GlyphAtlas&&) noexcept;

        /**
         * @brief Packs a `width` by `height` rectangle of `coverage` (one byte a pixel, rows top to bottom) and uploads
         * it. None when it is empty or larger than a page can ever be, or the coverage is the wrong size.
         */
        std::optional<AtlasRegion> Add(std::uint32_t width, std::uint32_t height, std::span<const std::byte> coverage);

        /** @brief The pages, oldest first; region `page` numbers index this. */
        [[nodiscard]] std::span<const AssetHandle<Image>> Pages() const noexcept;

      private:
        void UnloadPages();

        std::unique_ptr<Detail::GlyphAtlasState> m_state;
    };
} // namespace Fenrir
