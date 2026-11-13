#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirText/GlyphAtlas.hpp"
#include "FenrirText/TextStyle.hpp"

namespace Fenrir
{
    namespace Detail
    {
        struct FontLibraryState;
    }

    /** @brief How a font library sets up its glyph atlases. */
    struct FontLibraryConfig
    {
        /** @brief Every atlas's page sizes and padding. */
        GlyphAtlasConfig atlas;
        /**
         * @brief The pixel size an SDF face (FontRendering::Sdf) is rasterised at, once for every size its text is laid
         * out at: its glyphs' positions and quads are scaled from this size.
         */
        float sdfSize = 48.0f;
        /**
         * @brief How far, in pixels at sdfSize, an SDF glyph's distance field reaches either side of its outline:
         * FreeType's `spread`, 2 to 32. Each SDF glyph's atlas rectangle is padded by it on every side.
         */
        std::uint32_t sdfSpread = 8;
    };

    /**
     * @brief The fonts FenrirText lays text out in (ADR-0062): the font family registry, the faces it has opened from
     * `Font` assets (the font cache, one FreeType face and HarfBuzz font per font and size) and their glyph atlases,
     * one per face, size and mode.
     *
     * A face is registered in bitmap or SDF mode (FontRendering). A bitmap face has an atlas per pixel size; an SDF
     * face is opened once, at FontLibraryConfig::sdfSize, and every size of it is shaped there and scaled, sharing the
     * one atlas. The mode belongs to the registration, so one font may be registered in both, under two families.
     *
     * A family name maps to faces by weight and style, and has a fallback list of other families. Text in a family is
     * tried, a character at a time, in its nearest face, then each fallback family's nearest face, then the embedded
     * default face (JetBrains Mono, under the SIL Open Font License), so text never vanishes; a name with no faces
     * goes straight to the default. The default face is also the family `default`.
     *
     * Main thread only, as the asset store is. The store must outlive the library, which unloads its atlases' pages
     * when it ends. Fonts are read once, when first used: replacing a font asset later does not reopen it.
     */
    class FontLibrary
    {
      public:
        /** @brief The family name the embedded default face is registered under. */
        static constexpr std::string_view DefaultFamily = "default";

        /** @brief A library with only the embedded default face, added to `store` as the built-in
         * `builtin:default-font`. */
        explicit FontLibrary(AssetStore& store, FontLibraryConfig config = {});
        ~FontLibrary();
        FontLibrary(const FontLibrary&) = delete;
        FontLibrary& operator=(const FontLibrary&) = delete;
        FontLibrary(FontLibrary&&) = delete;
        FontLibrary& operator=(FontLibrary&&) = delete;

        /**
         * @brief Registers `font` as the face of `family` at `weight` and `style`, rasterised in `rendering`'s mode,
         * replacing any face registered there. Paragraphs laid out in this library lay out again on their next Layout.
         */
        void AddFace(std::string_view family, AssetHandle<Font> font, std::uint16_t weight = FontWeightNormal,
                     FontStyle style = FontStyle::Normal, FontRendering rendering = FontRendering::Bitmap);

        /**
         * @brief Sets the families tried, in order, for a character `family`'s faces lack, before the default face.
         * Only the named families' own faces are tried, not their fallbacks. Paragraphs lay out again, as AddFace.
         */
        void SetFallbacks(std::string_view family, std::vector<std::string> fallbacks);

        /** @brief The embedded default face's font asset. */
        [[nodiscard]] AssetHandle<Font> DefaultFont() const noexcept;

        /**
         * @brief The faces text in `family` at `weight` and `style` is tried in, in order: the family's nearest face,
         * each fallback family's nearest face, and the default face last, each once. The nearest face is the one of the
         * style asked for, if the family has any, of the nearest weight: from 400 up the nearest heavier face, else
         * the nearest lighter, and below 400 the other way round, as CSS's font matching.
         */
        [[nodiscard]] std::vector<AssetHandle<Font>> FaceChain(std::string_view family, std::uint16_t weight,
                                                               FontStyle style) const;

        /**
         * @brief The pages of the atlas of `font` at `size` pixels in `rendering`'s mode; empty if nothing in it was
         * laid out yet. An SDF face has one atlas for every size, so `size` is not read for FontRendering::Sdf.
         */
        [[nodiscard]] std::span<const AssetHandle<Image>> AtlasPages(
            AssetHandle<Font> font, float size, FontRendering rendering = FontRendering::Bitmap) const;

      private:
        friend class Paragraph;

        // The opened faces and atlases a paragraph lays out with.
        [[nodiscard]] Detail::FontLibraryState& State() const noexcept;

        std::unique_ptr<Detail::FontLibraryState> m_state;
    };
} // namespace Fenrir
