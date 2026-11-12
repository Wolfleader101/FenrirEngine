# 06: Painter text

**What to build:** the painter draws FenrirText's paragraphs and glyph runs as atlas quads, batched with its shapes under the same clips and transforms (spec: "FenrirPainter" draws; ADR-0060, ADR-0062).

- FenrirPainter gains its FenrirText dependency; `DrawText(paragraph, point)` and `DrawGlyphs(run, point)` emit one quad per glyph with shape kind "glyph", the atlas page in a texture slot, the text colour as the paint, positioned at the paragraph's quarter-pixel offsets scaled by the root scale and snapped when pixel snapping is on; a paragraph whose font was not yet rasterised at this size triggers the atlas on the spot.
- A text shadow helper draws the run offset and tinted first; a decoration (underline, strike) is a rectangle draw from the run's metrics.
- The shader's glyph kind samples R8 coverage and multiplies the paint, in the root's blend space.

**Blocked by:** 02, 03

**Touches:** `packages/FenrirPainter/`

**Status:** resolved

- [x] Draw-list tests: a paragraph of N visible glyphs emits N quads over the right atlas rectangles; glyphs of two faces split into two commands by texture; text under a rotated transform lands rotated; text under a clip gets the clip's data; a shadow draws before its text.
- [x] GL test (skipped without a display): a word drawn to an image target reads back darker pixels along its baseline than above it, and the same word at two sizes differs in extent as the font's metrics predict.
- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
