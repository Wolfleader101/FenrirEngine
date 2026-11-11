# FenrirText

Fonts, shaping, paragraph layout and glyph atlases (ADR-0061, ADR-0062): text in any script, laid out to a width, with glyphs rasterised on first use into atlases the renderer can sample. It knows no renderer, painter or UI; FenrirPainter draws its glyph runs. It depends on FenrirAssets (fonts and atlas pages are assets), FenrirDraw2D (`Rect2D`) and FenrirMath, and is built with `FENRIR_ENABLE_TEXT` (on by default; it needs `FENRIR_ENABLE_ASSETS`).

Its text libraries are submodules under `libs/`, each pinned, built static, warnings silenced and marked `SYSTEM`, as FenrirWindow's SDL is, and linked PRIVATE, so no user of FenrirText sees their headers:

| Library | Version | Licence | For |
| --- | --- | --- | --- |
| FreeType | 2.15.0 | FreeType (BSD-style) | Faces, metrics, rasterising (auto-hinter, grayscale) |
| HarfBuzz | 15.1.0 | MIT | Shaping, through its FreeType integration |
| SheenBidi | 2.9.0 | Apache 2.0 | Bidi levels (UAX#9), line runs, scripts |
| libunibreak | 8.0 | zlib | Line breaks (UAX#14), grapheme boundaries (UAX#29) |

No ICU: SheenBidi and libunibreak together are a fraction of its size.

HarfBuzz's test data holds file names long enough that a checkout deeper than about 35 characters passes Windows' 260-character limit; in such a checkout (a git worktree) run `git -C packages/FenrirText/libs/harfbuzz config core.longpaths true` before `git submodule update`.

## Fonts

FenrirAssets's `Font` asset is a TrueType or OpenType file's bytes (`.ttf`, `.otf`, `.ttc`), loaded through the asset store like an image: `store.Load<Font>("fonts/Inter-Regular.ttf", owner)`.

`FenrirText/FontLibrary.hpp`. A `FontLibrary` over the store is the font family registry, the font cache (one FreeType face and HarfBuzz font per font and size, sizes rounded to a quarter pixel) and the atlases:

- **`AddFace(family, font, weight, style)`** registers a face; **`SetFallbacks(family, {...})`** names the families tried, in order, for a character the family lacks.
- **The embedded default face**, JetBrains Mono (SIL Open Font License, `Resources/Fonts`; the debug font's file, embedded again so FenrirText stays independent of the renderer), is the family `default`, the built-in asset `builtin:default-font`, and the last face every family falls back to, so text never vanishes. A family with no faces resolves to it.
- **`FaceChain(family, weight, style)`** says which faces text is tried in: the family's nearest face (its style if it has one, then the nearest weight, heavier first from 400 up), each fallback family's nearest face, then the default.
- Changing the families makes every paragraph lay out again on its next `Layout`.

## Paragraphs

`FenrirText/Paragraph.hpp` and `FenrirText/TextStyle.hpp`. A `Paragraph` holds runs of UTF-8 text, each in a `TextStyle` (family, size in pixels, weight, style, colour, letter spacing, decoration), and a `ParagraphStyle` (alignment, line height, max lines, ellipsis, white space, base direction). `Layout(fonts, width)` lays it out, or does nothing and returns false when the text, styles, width and families are unchanged since it last did:

1. White space is collapsed or kept (`WhiteSpace`, as CSS's `white-space`).
2. The text is split into items by style, bidi level (SheenBidi), script (SheenBidi's script locator) and face (per grapheme, the first face of the chain that has all of it).
3. Each item is shaped by HarfBuzz, with kerning and ligatures; letter spacing turns ligatures off.
4. Lines break greedily at libunibreak's UAX#14 opportunities within the width (`UnlimitedWidth` for none); trailing spaces hang. `maxLines` and `ellipsis` cut the text, the ellipsis in the face and style the text ends in.
5. Each line is ordered by SheenBidi's line runs, aligned, and given a line box: its faces' ascent and descent, or `lineHeight` times its largest size, with the extra split above and below.
6. Glyphs are rasterised into the atlases on first use and placed at quarter-pixel pen positions.

It answers `Size()`, `Lines()` (byte range, top, height, baseline, ascent, descent, left, width), `Runs()` (glyph runs of one face, page, style and line, left to right, each glyph with its pen position, its quad in whole pixels and its atlas rectangle), `RangeRects(begin, end)` (the boxes of a character range), `CharacterAt(point)` (the caret position under a point) and `GraphemeBoundaries()` with `NextGrapheme` and `PreviousGrapheme` (UAX#29). Character positions are byte offsets into `Text()`.

## Atlases

`FenrirText/GlyphAtlas.hpp`. One `GlyphAtlas` per face and size: R8 coverage pages, each an `Image` asset it owns, skyline-packed (bottom-left fit) with a pixel of padding. A full page doubles its side up to a limit (2048 by default), then the atlas opens another page. Each rectangle is uploaded as it is added through `AssetStore::UpdateImage`, so `AssetStore::ImageUpdatesSince` names exactly the rectangles added since a revision. Glyphs are rasterised with FreeType's auto-hinter in its light mode (vertical fitting only, so quarter-pixel positions keep their spacing) and grayscale anti-aliasing, once per glyph and quarter-pixel offset.

Not yet: the SDF mode (ADR-0062's opt-in per font), colour emoji, variable fonts and system font fallback. The renderer's `DebugFont` does not use FenrirText.

## Tests

`FenrirTextTests` (with `FENRIR_BUILD_TESTS`) lays paragraphs out in HarfBuzz's test fonts (Roboto, Amiri, Noto Sans Devanagari, read from the submodule) and the embedded face, checks the atlas's packing, growth and uploads through the store, and fails the build if a public header reaches the text libraries.
