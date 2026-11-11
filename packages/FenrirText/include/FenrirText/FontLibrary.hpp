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
    };

    /**
     * @brief The fonts FenrirText lays text out in (ADR-0062): the font family registry, the faces it has opened from
     * `Font` assets (the font cache, one FreeType face and HarfBuzz font per font and size) and their glyph atlases,
     * one per face and size.
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
         * @brief Registers `font` as the face of `family` at `weight` and `style`, replacing any face registered there.
         * Paragraphs laid out in this library lay out again on their next Layout.
         */
        void AddFace(std::string_view family, AssetHandle<Font> font, std::uint16_t weight = FontWeightNormal,
                     FontStyle style = FontStyle::Normal);

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
         * style asked for, if the family has any, of the nearest weight (heavier first from 400 up, lighter first
         * below, as CSS's font matching).
         */
        [[nodiscard]] std::vector<AssetHandle<Font>> FaceChain(std::string_view family, std::uint16_t weight,
                                                               FontStyle style) const;

        /** @brief The pages of the atlas of `font` at `size` pixels; empty if nothing at that size was laid out yet. */
        [[nodiscard]] std::span<const AssetHandle<Image>> AtlasPages(AssetHandle<Font> font, float size) const;

        /** @brief The store the fonts are read from and the atlases' pages live in. */
        [[nodiscard]] AssetStore& Store() const noexcept;

        /** @brief FenrirText's own: the opened faces and atlases a paragraph lays out with. */
        [[nodiscard]] Detail::FontLibraryState& State() const noexcept;

      private:
        std::unique_ptr<Detail::FontLibraryState> m_state;
    };
} // namespace Fenrir
