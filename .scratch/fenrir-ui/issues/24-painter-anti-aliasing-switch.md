# 24: The anti-aliasing switch: a painter option and the UI app resource's switch

**What to build:** a game can turn off the painter's anti-aliasing for a pixel-art UI, and FenrirUI's app resource carries the switch the spec lists (spec: "FenrirUI" switches, "per UI app resource, anti-aliasing (fringe and analytic AA on, or off for pixel-art UIs)"; ADR-0063's "each technique is a switch"; ADR-0060).

Ticket 07 left the UI app resource's anti-aliasing switch out because `PainterConfig` had no option for it to set; this ticket adds both halves.

- **Painter**: `PainterConfig` gains an anti-aliasing option, on by default. Off, tessellated paths and strokes emit no fringe ring, and analytic shapes (rectangles, rounded rectangles, borders, shadows, circles, arcs, rings) and the per-pixel clip resolve coverage to a hard edge at the half-pixel rather than over one pixel. Text is unaffected: a glyph's coverage is the atlas's.
- **FenrirUI**: the UI app resource's anti-aliasing switch, on by default, sets the option for every root's painter. The README's switch list names it.

**Blocked by:** 06, 07

**Touches:** `packages/FenrirPainter/`, `packages/FenrirUI/`

**Status:** resolved

- [x] Draw-list tests: with anti-aliasing off a filled path emits no fringe triangles and a stroke none either; analytic shapes carry the hard-edge flag in their vertices; with it on, output is unchanged from today.
- [x] Headless App test: the UI app resource's switch off reaches the painter of every root, read back through the captured list.
- [x] GL test (skipped without a display): a rounded rectangle drawn with anti-aliasing off reads back only fully covered or empty pixels along its edge.
- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

- Built on `ui/24-painter-anti-aliasing-switch`. `PainterConfig::antiAliasing` (on by default) and `UISettings::antiAliasing` (on by default, read into every root's painter each frame). Off, `Tessellate` gets a zero fringe and returns one vertex per boundary point with only the solid triangles; every vertex the painter emits carries `PainterFlags::HardEdge` (bit 4), and the shader's `Coverage` returns 1 when the pixel centre is inside and 0 otherwise, for every analytic shape and the per-pixel clip. Two readings of the ticket: a shadow's blur stays a blur (only its box cut-out and a blur under half a pixel go hard), and a stroke thinner than a pixel is still drawn a pixel wide but no longer faded to its width. Glyph quads carry the flag too, but the glyph branches never read `Coverage`, so only a clip on text goes hard. The switch is a `bool`: ADR-0063 and spec story 68 name an "anti-aliasing mode", and on/off is the only mode the spec lists.
