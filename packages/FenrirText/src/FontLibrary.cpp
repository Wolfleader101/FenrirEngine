#include "FenrirText/FontLibrary.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <utility>

#include <hb-ft.h>
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

        std::uint64_t FontKey(AssetHandle<Font> font)
        {
            return (std::uint64_t{font.generation} << 32) | font.index;
        }

        SizedFace::~SizedFace()
        {
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
            // Faces before the library that made them.
            faces.clear();
            if (library != nullptr)
            {
                FT_Done_FreeType(library);
            }
        }

        SizedFace* FontLibraryState::Face(AssetHandle<Font> font, float size)
        {
            const std::uint64_t key = FontKey(font);
            const std::uint32_t quarters = QuarterPixels(size);
            if (const auto found = faces.find({key, quarters}); found != faces.end())
            {
                return found->second.get();
            }
            // Size zero stands for the whole font: no size of it opens.
            if (failed.contains({key, 0}) || failed.contains({key, quarters}))
            {
                return nullptr;
            }
            const auto fail = [&](const char* why, bool everySize) -> SizedFace* {
                failed.insert({key, everySize ? 0u : quarters});
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

            SizedFace* result = sized.get();
            faces.emplace(std::pair{key, quarters}, std::move(sized));
            return result;
        }

        const RasterisedGlyph& FontLibraryState::Glyph(SizedFace& face, std::uint32_t glyph, std::uint32_t quarter)
        {
            const std::uint64_t key = std::uint64_t{glyph} * 4 + quarter;
            if (const auto found = face.glyphs.find(key); found != face.glyphs.end())
            {
                return found->second;
            }
            RasterisedGlyph& rasterised = face.glyphs[key];
            // FreeType's auto-hinter in its light mode, which fits outlines vertically only, so quarter-pixel
            // positions across the line keep their spacing; grayscale anti-aliasing (ADR-0062).
            if (FT_Load_Glyph(face.face, glyph, FT_LOAD_FORCE_AUTOHINT | FT_LOAD_TARGET_LIGHT) != 0)
            {
                return rasterised;
            }
            FT_GlyphSlot slot = face.face->glyph;
            if (slot->format == FT_GLYPH_FORMAT_OUTLINE)
            {
                // A quarter pixel is 16 in 26.6.
                FT_Outline_Translate(&slot->outline, static_cast<FT_Pos>(quarter) * 16, 0);
            }
            if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0)
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

        std::optional<AssetHandle<Font>> FontLibraryState::Nearest(const FontFamily& family, std::uint16_t weight,
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
            return best->font;
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
        FontData font;
        font.bytes.resize(std::size(Resources::DefaultFont));
        std::memcpy(font.bytes.data(), Resources::DefaultFont, font.bytes.size());
        // A second library over the same store adds the same bytes under the same name, which keeps the first one's
        // handle (AddBuiltin replaces): both share the one asset.
        m_state->defaultFont = store.AddBuiltin<Font>("builtin:default-font", std::move(font));
        AddFace(DefaultFamily, m_state->defaultFont);
    }

    FontLibrary::~FontLibrary() = default;

    void FontLibrary::AddFace(std::string_view family, AssetHandle<Font> font, std::uint16_t weight, FontStyle style)
    {
        Detail::FontFamily& entry = m_state->families[std::string(family)];
        std::erase_if(entry.faces,
                      [&](const Detail::RegisteredFace& face) { return face.weight == weight && face.style == style; });
        entry.faces.push_back({font, weight, style});
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
        const auto add = [&](std::optional<AssetHandle<Font>> font) {
            if (font && std::ranges::find(chain, *font) == chain.end())
            {
                chain.push_back(*font);
            }
        };
        if (const auto found = m_state->families.find(family); found != m_state->families.end())
        {
            add(m_state->Nearest(found->second, weight, style));
            for (const std::string& fallback : found->second.fallbacks)
            {
                if (const auto other = m_state->families.find(fallback); other != m_state->families.end())
                {
                    add(m_state->Nearest(other->second, weight, style));
                }
            }
        }
        add(m_state->defaultFont);
        return chain;
    }

    std::span<const AssetHandle<Image>> FontLibrary::AtlasPages(AssetHandle<Font> font, float size) const
    {
        const auto found = m_state->faces.find({Detail::FontKey(font), Detail::QuarterPixels(size)});
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
