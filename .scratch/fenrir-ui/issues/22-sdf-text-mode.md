# 22: SDF text mode

**What to build:** a font can opt into a distance-field mode so its text scales and animates without blur, for world-space and large or moving text (spec: "FenrirText" atlases; ADR-0062).

- FenrirText rasterises a face in FreeType's single-channel SDF render mode at one size per face with a spread, into an atlas of its own, and lays out paragraphs in that mode with positions scaled from the SDF size; the painter's glyph kind "sdf glyph" reconstructs coverage from the distance for any size with a one-pixel anti-aliasing width in screen pixels; `font-family` registration and `@font-face`'s descriptor (`font-rendering: sdf`, stored by ticket 16) choose the mode per face; a mixed UI uses both.
- The demo's HUD compass labels and the name plate of ticket 23 use it.

**Blocked by:** 06, 16

**Touches:** `packages/FenrirText/`, `packages/FenrirPainter/`, `packages/FenrirUI/`

**Status:** resolved

- [x] FenrirText tests: an SDF atlas holds one entry per glyph regardless of size; a paragraph in SDF mode at two sizes has proportional extents.
- [x] Draw-list tests: a label in an SDF family emits quads with the SDF kind and the SDF atlas.
- [x] GL test (skipped without a display): a glyph scaled by 4 through the SDF path reads back a smooth edge, and the same glyph through the bitmap path reads back blocky.
- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
