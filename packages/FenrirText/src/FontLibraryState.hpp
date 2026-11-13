#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>

#include "FenrirAssets/AssetStore.hpp"
#include "FenrirText/FontLibrary.hpp"
#include "FenrirText/GlyphAtlas.hpp"

namespace Fenrir::Detail
{
    /** @brief A number no font library's generation has had yet. */
    [[nodiscard]] std::uint64_t NextGeneration();

    /** @brief A glyph rasterised at one quarter-pixel offset: where it is in the atlas, and its bitmap's bearing. */
    struct RasterisedGlyph
    {
        /** @brief None for a glyph with nothing to draw (a space). */
        std::optional<AtlasRegion> atlas;
        /** @brief The bitmap's left edge right of the pen, and its top above the baseline, in whole pixels. */
        int left = 0;
        int top = 0;
    };

    /**
     * @brief One font opened at one size and mode: the font cache's entry (ADR-0062), with its glyph atlas.
     *
     * A bitmap face, and the SDF raster face an SDF font is opened as once at the SDF size, own their FreeType face,
     * HarfBuzz font, atlas and glyphs. An SDF face at any other size borrows its raster face's (`raster`), and scales
     * what it shapes and its metrics from the raster face's size to its own (`scale`).
     */
    struct SizedFace
    {
        SizedFace(AssetStore& store, const GlyphAtlasConfig& config) : atlas(store, config)
        {
        }
        ~SizedFace();
        SizedFace(const SizedFace&) = delete;
        SizedFace& operator=(const SizedFace&) = delete;
        SizedFace(SizedFace&&) = delete;
        SizedFace& operator=(SizedFace&&) = delete;

        AssetHandle<Font> font;
        /** @brief The size in pixels, a multiple of a quarter pixel. */
        float size = 0.0f;
        FontRendering rendering = FontRendering::Bitmap;
        /** @brief The SDF raster face whose FreeType face, HarfBuzz font, atlas and glyphs this face uses; null for its
         * own. */
        SizedFace* raster = nullptr;
        /** @brief This face's pixels per pixel of what it shapes and rasterises: its size over its raster face's. */
        float scale = 1.0f;
        /** @brief Owned unless `raster` is set. */
        FT_Face face = nullptr;
        hb_font_t* shaper = nullptr;
        /** @brief The face's ascent above and descent below the baseline, and its line gap, in pixels. */
        float ascent = 0.0f;
        float descent = 0.0f;
        float lineGap = 0.0f;
        float underlineOffset = 0.0f;
        float underlineThickness = 0.0f;
        GlyphAtlas atlas;
        /** @brief By glyph index times four plus quarter-pixel offset. */
        std::unordered_map<std::uint64_t, RasterisedGlyph> glyphs;

        /** @brief Whether the face maps `codepoint` to a glyph. */
        [[nodiscard]] bool Has(char32_t codepoint) const;

        /** @brief The face that rasterises this one's glyphs, and keeps them in its atlas: `raster`, or itself. */
        [[nodiscard]] SizedFace& Raster() noexcept
        {
            return raster != nullptr ? *raster : *this;
        }
    };

    /** @brief One face a family registered: its font, weight and style. */
    struct RegisteredFace
    {
        AssetHandle<Font> font;
        std::uint16_t weight = FontWeightNormal;
        FontStyle style = FontStyle::Normal;
        FontRendering rendering = FontRendering::Bitmap;
    };

    /** @brief A font text is tried in, and the mode its family registered it in. */
    struct ChainFace
    {
        AssetHandle<Font> font;
        FontRendering rendering = FontRendering::Bitmap;

        friend bool operator==(const ChainFace&, const ChainFace&) = default;
    };

    struct FontFamily
    {
        std::vector<RegisteredFace> faces;
        std::vector<std::string> fallbacks;
    };

    struct FontLibraryState
    {
        FontLibraryState() = default;
        ~FontLibraryState();
        FontLibraryState(const FontLibraryState&) = delete;
        FontLibraryState& operator=(const FontLibraryState&) = delete;
        FontLibraryState(FontLibraryState&&) = delete;
        FontLibraryState& operator=(FontLibraryState&&) = delete;

        AssetStore* store = nullptr;
        FontLibraryConfig config;
        FT_Library library = nullptr;
        AssetHandle<Font> defaultFont;
        std::map<std::string, FontFamily, std::less<>> families;
        /**
         * @brief Unique among every library's, and renewed by every change to the families (NextGeneration), so a
         * laid-out paragraph knows to lay out again when its families changed or it is given another library.
         */
        std::uint64_t generation = NextGeneration();
        /** @brief Each opened font's bytes, copied from its asset: FreeType reads a memory face in place. */
        std::map<std::uint64_t, std::shared_ptr<const std::vector<std::byte>>> bytes;
        /** @brief By font and FaceKey. */
        std::map<std::pair<std::uint64_t, std::uint32_t>, std::unique_ptr<SizedFace>> faces;
        /** @brief Each SDF font's raster face, at the SDF size, by font; the SDF faces in `faces` borrow from them. */
        std::map<std::uint64_t, std::unique_ptr<SizedFace>> sdfRasters;

        /**
         * @brief `font` in `rendering`'s mode at `size` pixels (rounded to a quarter pixel), opening it on first use;
         * null, with a logged error once, when the handle does not resolve or FreeType cannot read the font.
         */
        [[nodiscard]] SizedFace* Face(AssetHandle<Font> font, FontRendering rendering, float size);

        /**
         * @brief The glyph `glyph` of `face` at quarter-pixel offset `quarter`, rasterised on first use into the atlas
         * of the face's raster face. An SDF glyph is rasterised once whatever the offset or size, at offset zero.
         */
        [[nodiscard]] const RasterisedGlyph& Glyph(SizedFace& face, std::uint32_t glyph, std::uint32_t quarter);

        /** @brief The registered face of `family` nearest `weight` and `style`, or none if it has no faces. */
        [[nodiscard]] std::optional<ChainFace> Nearest(const FontFamily& family, std::uint16_t weight,
                                                       FontStyle style) const;

        /** @brief FontLibrary::FaceChain with each font's mode: the default face last, in bitmap mode. */
        [[nodiscard]] std::vector<ChainFace> Chain(std::string_view family, std::uint16_t weight,
                                                   FontStyle style) const;

        /** @brief `font` opened in FreeType and HarfBuzz at `quarters` quarter pixels, its metrics read; null if not.
         */
        [[nodiscard]] std::unique_ptr<SizedFace> Open(AssetHandle<Font> font, std::uint32_t quarters);

        /** @brief `font`'s SDF raster face at the SDF size, opened on first use; null, as Face, if it cannot be. */
        [[nodiscard]] SizedFace* SdfRaster(AssetHandle<Font> font);

        /**
         * @brief The fonts and sizes that failed to open, so each is logged once: by font and size in quarter pixels,
         * size zero for a font that opens at no size.
         */
        std::set<std::pair<std::uint64_t, std::uint32_t>> failed;
    };

    /** @brief `size` rounded to a quarter pixel, in quarter pixels, at least one. */
    [[nodiscard]] std::uint32_t QuarterPixels(float size);

    /** @brief A face's key among a font's faces: its size in quarter pixels, the top bit set for an SDF face. */
    [[nodiscard]] std::uint32_t FaceKey(FontRendering rendering, float size);

    /** @brief A font handle as one number, for maps. */
    [[nodiscard]] std::uint64_t FontKey(AssetHandle<Font> font);
} // namespace Fenrir::Detail
