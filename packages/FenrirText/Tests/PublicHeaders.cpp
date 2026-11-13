// FenrirText keeps its text libraries to itself (ADR-0062): only its sources include FreeType, HarfBuzz, SheenBidi or
// libunibreak, and they are linked PRIVATE. This file includes its headers before anything else, so if one of them
// ever reaches for those libraries, or one of their include paths reaches a user of FenrirText (this test target is
// one), the build fails here.
#include "FenrirText/FontLibrary.hpp"
#include "FenrirText/GlyphAtlas.hpp"
#include "FenrirText/Paragraph.hpp"
#include "FenrirText/TextSegments.hpp"
#include "FenrirText/TextStyle.hpp"

#if defined(FT_FREETYPE_H) || defined(HB_H) || defined(_SB_PUBLIC_SHEENBIDI_H) || defined(LINEBREAK_H) || \
    defined(WORDBREAK_H)
#error "A FenrirText header includes one of its text libraries."
#endif

#if __has_include(<ft2build.h>) || __has_include(<hb.h>) || __has_include(<SheenBidi/SheenBidi.h>) ||                \
    __has_include(<linebreak.h>)
#error "A text library's include path reaches a user of FenrirText."
#endif

#if __has_include("FenrirRenderer/Renderer.hpp") || __has_include("FenrirPainter/Painter.hpp")
#error "FenrirText depends on the renderer or the painter: it may depend on FenrirAssets, FenrirDraw2D and FenrirMath."
#endif

#include <catch2/catch_test_macros.hpp>

TEST_CASE("FenrirText's headers compile without its text libraries", "[text][seam]")
{
    const Fenrir::Paragraph paragraph;
    INFO("a new paragraph has no text and no lines");
    REQUIRE(paragraph.Text().empty());
    REQUIRE(paragraph.Lines().empty());
}
