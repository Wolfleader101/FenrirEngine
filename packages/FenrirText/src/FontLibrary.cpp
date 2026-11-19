#include "FenrirText/FontLibrary.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <utility>

#include <hb-ft.h>
#include FT_MODULE_H
#include FT_OUTLINE_H

#include "FenrirLogger/ILogger.hpp"
#include "FenrirTextResources/DefaultFont.hpp"
#include "FontLibraryState.hpp"

namespace Fenrir
{
    namespace Detail
    {
        std::uint64_t NextGeneration()
        {
            static std::atomic<std::uint64_t> next{1};
            return next++;
        }

        std::uint32_t QuarterPixels(float size)
        {
            if (!std::isfinite(size) || size < 0.25f)
            {
                return 1;
            }
            return static_cast<std::uint32_t>(std::lround(size * 4.0f));
        }

        namespace
        {
            /** @brief FaceKey's mark of an SDF face, above every size in quarter pixels. */
            constexpr std::uint32_t SdfFaceBit = 1u << 31u;
        } // namespace

        std::uint32_t FaceKey(FontRendering rendering, float size)
        {
            const std::uint32_t quarters = std::min(QuarterPixels(size), SdfFaceBit - 1);
            return rendering == FontRendering::Sdf ? quarters | SdfFaceBit : quarters;
        }

        std::uint64_t FontKey(AssetHandle<Font> font)
        {
            return (std::uint64_t{font.generation} << 32) | font.index;
        }

        SizedFace::~SizedFace()
        {
            // An SDF face at a size borrows its raster face's.
            if (raster != nullptr)
            {
                return;
            }
            if (shaper != nullptr)
            {
                hb_font_destroy(shaper);
            }
            if (face != nullptr)
            {
                FT_Done_Face(face);
            }
        }

        bool SizedFace::Has(char32_t codepoint) const
        {
            return FT_Get_Char_Index(face, codepoint) != 0;
        }

        FontLibraryState::~FontLibraryState()
        {
            // Faces before the library that made them, and the faces that borrow from a raster face before it.
            faces.clear();
            sdfRasters.clear();
            if (library != nullptr)
            {
                FT_Done_FreeType(library);
            }
        }

        std::unique_ptr<SizedFace> FontLibraryState::Open(AssetHandle<Font> font, std::uint32_t faceKey)
        {
            const std::uint64_t key = FontKey(font);
            const std::uint32_t quarters = faceKey & ~SdfFaceBit;
            const auto fail = [&](const char* why, bool everySize) -> std::unique_ptr<SizedFace> {
                failed.insert({key, everySize ? 0u : faceKey});
                store->Logger().Error("FenrirText could not open a font: {}", why);
                return nullptr;
            };

            auto& fontBytes = bytes[key];
            if (!fontBytes)
            {
                const FontData* data = store->TryGet(font);
                if (data == nullptr)
                {
                    bytes.erase(key);
                    return fail("its handle is empty or stale", true);
                }
                fontBytes = std::make_shared<const std::vector<std::byte>>(data->bytes);
            }

            auto sized = std::make_unique<SizedFace>(*store, config.atlas);
            sized->font = font;
            sized->size = static_cast<float>(quarters) / 4.0f;
            if (FT_New_Memory_Face(library, reinterpret_cast<const FT_Byte*>(fontBytes->data()),
                                   static_cast<FT_Long>(fontBytes->size()), 0, &sized->face) != 0)
            {
                return fail("FreeType cannot read it", true);
            }
            // 26.6 fixed point at 72 dots an inch, so a point is a pixel: the size in sixty-fourths of a pixel.
            if (FT_Set_Char_Size(sized->face, 0, static_cast<FT_F26Dot6>(quarters) * 16, 72, 72) != 0)
            {
                return fail("FreeType cannot size it at this size", false);
            }
            sized->shaper = hb_ft_font_create_referenced(sized->face);
            // Shaped from unhinted outlines, so advances are exact and glyphs land at quarter pixels; the hinting is
            // the rasteriser's.
            hb_ft_font_set_load_flags(sized->shaper, FT_LOAD_NO_HINTING);

            const FT_Face face = sized->face;
            const float scale = face->units_per_EM != 0 ? sized->size / static_cast<float>(face->units_per_EM) : 0.0f;
            sized->ascent = static_cast<float>(face->ascender) * scale;
            sized->descent = static_cast<float>(-face->descender) * scale;
            sized->lineGap = std::max(0.0f, static_cast<float>(face->height) * scale - sized->ascent - sized->descent);
            sized->underlineOffset = static_cast<float>(-face->underline_position) * scale;
            sized->underlineThickness = std::max(1.0f, static_cast<float>(face->underline_thickness) * scale);
            return sized;
        }

        SizedFace* FontLibraryState::Face(AssetHandle<Font> font, FontRendering rendering, float size)
        {
            const std::uint64_t key = FontKey(font);
            const std::uint32_t faceKey = FaceKey(rendering, size);
            if (const auto found = faces.find({key, faceKey}); found != faces.end())
            {
                return found->second.get();
            }
            // Size zero stands for the whole font: no size of it opens.
            if (failed.contains({key, 0}) || failed.contains({key, faceKey}))
            {
                return nullptr;
            }
            if (rendering == FontRendering::Bitmap)
            {
                std::unique_ptr<SizedFace> sized = Open(font, faceKey);
                if (!sized)
                {
                    return nullptr;
                }
                SizedFace* result = sized.get();
                faces.emplace(std::pair{key, faceKey}, std::move(sized));
                return result;
            }

            // Every size of an SDF font is shaped and rasterised by one face at the SDF size, and scaled from it.
            SizedFace* raster = SdfRaster(font);
            if (raster == nullptr)
            {
                return nullptr;
            }
            auto sized = std::make_unique<SizedFace>(*store, config.atlas);
            sized->font = font;
            sized->size = static_cast<float>(QuarterPixels(size)) / 4.0f;
            sized->rendering = FontRendering::Sdf;
            sized->raster = raster;
            sized->scale = sized->size / raster->size;
            sized->face = raster->face;
            sized->shaper = raster->shaper;
            sized->ascent = raster->ascent * sized->scale;
            sized->descent = raster->descent * sized->scale;
            sized->lineGap = raster->lineGap * sized->scale;
            sized->underlineOffset = raster->underlineOffset * sized->scale;
            sized->underlineThickness = std::max(1.0f, raster->underlineThickness * sized->scale);
            SizedFace* result = sized.get();
            faces.emplace(std::pair{key, faceKey}, std::move(sized));
            return result;
        }

        SizedFace* FontLibraryState::SdfRaster(AssetHandle<Font> font)
        {
            const std::uint64_t key = FontKey(font);
            if (const auto found = sdfRasters.find(key); found != sdfRasters.end())
            {
                return found->second.get();
            }
            // A failure to size the raster face is recorded under the SDF face key at the SDF size, apart from the
            // bitmap face of that size; a font that opens at no size fails in both modes.
            const std::uint32_t rasterKey = FaceKey(FontRendering::Sdf, config.sdfSize);
            if (failed.contains({key, 0}) || failed.contains({key, rasterKey}))
            {
                return nullptr;
            }
            std::unique_ptr<SizedFace> raster = Open(font, rasterKey);
            if (!raster)
            {
                return nullptr;
            }
            raster->rendering = FontRendering::Sdf;
            SizedFace* result = raster.get();
            sdfRasters.emplace(key, std::move(raster));
            return result;
        }

        const RasterisedGlyph& FontLibraryState::Glyph(SizedFace& sized, std::uint32_t glyph, std::uint32_t quarter)
        {
            SizedFace& face = sized.Raster();
            const bool sdf = face.rendering == FontRendering::Sdf;
            if (sdf)
            {
                quarter = 0;
            }
            const std::uint64_t key = std::uint64_t{glyph} * 4 + quarter;
            if (const auto found = face.glyphs.find(key); found != face.glyphs.end())
            {
                return found->second;
            }
            RasterisedGlyph& rasterised = face.glyphs[key];
            // Bitmap mode: FreeType's auto-hinter in its light mode, which fits outlines vertically only, so
            // quarter-pixel positions across the line keep their spacing; grayscale anti-aliasing (ADR-0062). SDF mode:
            // the unhinted outline, whose distance field every size is scaled from.
            const FT_Int32 loadFlags =
                sdf ? FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP : FT_LOAD_FORCE_AUTOHINT | FT_LOAD_TARGET_LIGHT;
            if (FT_Load_Glyph(face.face, glyph, loadFlags) != 0)
            {
                return rasterised;
            }
            FT_GlyphSlot slot = face.face->glyph;
            if (slot->format == FT_GLYPH_FORMAT_OUTLINE && quarter != 0)
            {
                // A quarter pixel is 16 in 26.6.
                FT_Outline_Translate(&slot->outline, static_cast<FT_Pos>(quarter) * 16, 0);
            }
            if (FT_Render_Glyph(slot, sdf ? FT_RENDER_MODE_SDF : FT_RENDER_MODE_NORMAL) != 0)
            {
                return rasterised;
            }
            const FT_Bitmap& bitmap = slot->bitmap;
            rasterised.left = slot->bitmap_left;
            rasterised.top = slot->bitmap_top;
            if (bitmap.width == 0 || bitmap.rows == 0 || bitmap.pixel_mode != FT_PIXEL_MODE_GRAY)
            {
                return rasterised;
            }
            std::vector<std::byte> coverage(std::size_t{bitmap.width} * bitmap.rows);
            for (unsigned int row = 0; row < bitmap.rows; ++row)
            {
                // A negative pitch stores the rows bottom up, so the top row is the last in memory.
                const unsigned char* source =
                    bitmap.pitch >= 0
                        ? bitmap.buffer + static_cast<std::ptrdiff_t>(row) * bitmap.pitch
                        : bitmap.buffer + static_cast<std::ptrdiff_t>(bitmap.rows - 1 - row) * -bitmap.pitch;
                std::memcpy(coverage.data() + std::size_t{row} * bitmap.width, source, bitmap.width);
            }
            rasterised.atlas = face.atlas.Add(bitmap.width, bitmap.rows, coverage);
            return rasterised;
        }

        std::optional<ChainFace> FontLibraryState::Nearest(const FontFamily& family, std::uint16_t weight,
                                                           FontStyle style) const
        {
            if (family.faces.empty())
            {
                return std::nullopt;
            }
            const bool hasStyle =
                std::ranges::any_of(family.faces, [&](const RegisteredFace& face) { return face.style == style; });
            const RegisteredFace* best = nullptr;
            int bestDistance = 0;
            for (const RegisteredFace& face : family.faces)
            {
                if (hasStyle && face.style != style)
                {
                    continue;
                }
                // CSS's font matching, simplified: from 400 up the nearest heavier face, else the nearest lighter;
                // below 400 the other way round. Every face on the wrong side ranks after every face on the right one.
                const int difference = static_cast<int>(face.weight) - static_cast<int>(weight);
                const bool wrongSide = weight >= FontWeightNormal ? difference < 0 : difference > 0;
                const int distance = std::abs(difference) + (wrongSide ? 1000 : 0);
                if (best == nullptr || distance < bestDistance)
                {
                    best = &face;
                    bestDistance = distance;
                }
            }
            return ChainFace{best->font, best->rendering};
        }

        std::vector<ChainFace> FontLibraryState::Chain(std::string_view family, std::uint16_t weight,
                                                       FontStyle style) const
        {
            std::vector<ChainFace> chain;
            const auto add = [&](std::optional<ChainFace> face) {
                if (face && std::ranges::find(chain, *face) == chain.end())
                {
                    chain.push_back(*face);
                }
            };
            if (const auto found = families.find(family); found != families.end())
            {
                add(Nearest(found->second, weight, style));
                for (const std::string& fallback : found->second.fallbacks)
                {
                    if (const auto other = families.find(fallback); other != families.end())
                    {
                        add(Nearest(other->second, weight, style));
                    }
                }
            }
            add(ChainFace{defaultFont, FontRendering::Bitmap});
            return chain;
        }
    } // namespace Detail

    FontLibrary::FontLibrary(AssetStore& store, FontLibraryConfig config)
        : m_state(std::make_unique<Detail::FontLibraryState>())
    {
        m_state->store = &store;
        m_state->config = config;
        if (FT_Init_FreeType(&m_state->library) != 0)
        {
            m_state->library = nullptr;
            store.Logger().Error("FenrirText could not start FreeType: no text will be laid out");
        }
        else
        {
            // FreeType's two SDF renderers, from outlines and from bitmaps, each take the spread.
            FT_UInt spread = std::clamp(config.sdfSpread, 2u, 32u);
            m_state->config.sdfSpread = spread;
            FT_Property_Set(m_state->library, "sdf", "spread", &spread);
            FT_Property_Set(m_state->library, "bsdf", "spread", &spread);
        }
        if (!std::isfinite(m_state->config.sdfSize) || m_state->config.sdfSize < 1.0f)
        {
            m_state->config.sdfSize = FontLibraryConfig{}.sdfSize;
        }
        FontData font;
        font.bytes.resize(std::size(Resources::DefaultFont));
        std::memcpy(font.bytes.data(), Resources::DefaultFont, font.bytes.size());
        // A second library over the same store adds the same bytes under the same name, which keeps the first one's
        // handle (AddBuiltin replaces): both share the one asset.
        m_state->defaultFont = store.AddBuiltin<Font>("builtin:default-font", std::move(font));
        AddFace(DefaultFamily, m_state->defaultFont);
    }

    FontLibrary::~FontLibrary() = default;

    void FontLibrary::AddFace(std::string_view family, AssetHandle<Font> font, std::uint16_t weight, FontStyle style,
                              FontRendering rendering)
    {
        Detail::FontFamily& entry = m_state->families[std::string(family)];
        std::erase_if(entry.faces,
                      [&](const Detail::RegisteredFace& face) { return face.weight == weight && face.style == style; });
        entry.faces.push_back({font, weight, style, rendering});
        m_state->generation = Detail::NextGeneration();
    }

    void FontLibrary::SetFallbacks(std::string_view family, std::vector<std::string> fallbacks)
    {
        m_state->families[std::string(family)].fallbacks = std::move(fallbacks);
        m_state->generation = Detail::NextGeneration();
    }

    AssetHandle<Font> FontLibrary::DefaultFont() const noexcept
    {
        return m_state->defaultFont;
    }

    std::vector<AssetHandle<Font>> FontLibrary::FaceChain(std::string_view family, std::uint16_t weight,
                                                          FontStyle style) const
    {
        std::vector<AssetHandle<Font>> chain;
        for (const Detail::ChainFace& face : m_state->Chain(family, weight, style))
        {
            // A font registered in both modes along the chain is listed once.
            if (std::ranges::find(chain, face.font) == chain.end())
            {
                chain.push_back(face.font);
            }
        }
        return chain;
    }

    std::span<const AssetHandle<Image>> FontLibrary::AtlasPages(AssetHandle<Font> font, float size,
                                                                FontRendering rendering) const
    {
        if (rendering == FontRendering::Sdf)
        {
            const auto raster = m_state->sdfRasters.find(Detail::FontKey(font));
            if (raster == m_state->sdfRasters.end())
            {
                return {};
            }
            return raster->second->atlas.Pages();
        }
        const auto found = m_state->faces.find({Detail::FontKey(font), Detail::FaceKey(rendering, size)});
        if (found == m_state->faces.end())
        {
            return {};
        }
        return found->second->atlas.Pages();
    }

    Detail::FontLibraryState& FontLibrary::State() const noexcept
    {
        return *m_state;
    }
} // namespace Fenrir
