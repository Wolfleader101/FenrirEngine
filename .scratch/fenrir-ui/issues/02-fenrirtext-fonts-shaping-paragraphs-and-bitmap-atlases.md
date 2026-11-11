# 02: FenrirText: fonts, shaping, paragraphs and bitmap atlases

**What to build:** a new package that turns a string, a font and a width into positioned glyphs in an atlas, for any script (spec: "FenrirText"; ADR-0062).

- **Submodules** HarfBuzz, FreeType, SheenBidi and libunibreak under the package's `libs`, pinned, built static, warnings silenced and marked `SYSTEM` as SDL is; HarfBuzz with its FreeType integration on. Behind `FENRIR_ENABLE_TEXT`, on by default.
- **`Font` asset** in FenrirAssets: the bytes of a TrueType or OpenType file, loaded through the asset store with a revision like an image.
- **Family registry**: a family name maps to faces by weight and style, with a fallback list, and the engine's embedded default face (JetBrains Mono, OFL, the debug font's file, embedded again here so the renderer stays independent) as the last fallback, so text never vanishes.
- **Paragraphs**: runs of text with a text style (family, size, weight, style, colour, letter spacing, decoration) and a paragraph style (alignment, line height, max lines, ellipsis, white-space); split by style, script and bidi level (SheenBidi), shaped by HarfBuzz, broken by libunibreak's UAX#14 at a width, reordered per line, aligned; answers size, line metrics, glyph runs with positions and atlas rectangles, the rectangle of a character range, the character at a point, and UAX#29 grapheme boundaries. Relaid out only when text, style or width changes.
- **Bitmap atlases**: one per face and pixel size, R8, skyline-packed, doubling to a limit then a second page, uploaded by dirty rectangle through the asset store's image update; glyphs rasterised with FreeType's auto-hinter and grayscale anti-aliasing at quarter-pixel positions.
- A README in the package's style; the renderer's `DebugFont` is untouched.

**Blocked by:** none

**Touches:** `packages/FenrirText/`, `packages/FenrirAssets/include/FenrirAssets/`, `packages/FenrirAssets/src/`, `packages/CMakeLists.txt`, `CMakeLists.txt`, `.gitmodules`, `README.md`

**Status:** resolved

- [x] Paragraph tests with HarfBuzz's test fonts and the embedded face: Latin kerning and ligatures, an Arabic run shaped and ordered right-to-left, mixed-direction lines, Devanagari conjuncts, wrapping at a width, ellipsis, alignment, line metrics, character rectangles and hit testing round-trip, grapheme boundaries over combining marks.
- [x] Atlas tests: packing reports non-overlapping rectangles, growth doubles then pages, uploads cover exactly the dirty rectangles.
- [x] Fallback tests: a missing glyph falls through the family's list to the embedded face; a missing family resolves to the embedded face.
- [ ] CI builds the submodules on every platform the build-and-test workflow runs (read it before claiming).
- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
