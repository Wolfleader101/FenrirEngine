---
status: proposed
---

# Text shapes with HarfBuzz and rasterises with FreeType into dynamic atlases, hinted bitmaps by default and SDF per font

A game UI needs text that a modern developer takes for granted: any script, ligatures and kerning, right-to-left runs, wrapping and alignment, at small sizes crisp and at large or scaled sizes smooth. The renderer's `DebugFont` (ADR-0025) is one embedded monospace face in a single-channel SDF atlas from stb_truetype, enough for screen messages and nothing more. Research on 2026-11-10 (`.scratch/fenrir-ui/research/painter-api-designs.md`, section 11) found that no painter does text alone: HarfBuzz shapes, a bidi library and a line breaker split and order runs, FreeType or stb_truetype rasterises, and a layout library sits on top; and that GPU glyphs are overwhelmingly bitmap atlases, with distance fields for scale independence. This ADR decides FenrirText's stack. Settled in a grilling session on 2026-11-10.

## The stack

- **HarfBuzz** (MIT) shapes each run: codepoints to positioned glyphs through OpenType GSUB/GPOS. **FreeType** loads faces and rasterises glyphs. Both are submodules, as Jolt, EnTT and meshoptimizer are.
- **No ICU.** Bidi is **SheenBidi** (Apache 2.0) and line breaking is **libunibreak** (zlib, UAX#14); together a fraction of ICU's size, which is tens of megabytes of data for what the UI needs of it.
- **A dynamic atlas per font and mode**: glyphs rasterised on first use into a skyline-packed texture that grows on demand and uploads by dirty rectangle, as fontstash and Dear ImGui's atlas do since 1.92. There are no prebaked glyph ranges.
- **Hinted grayscale bitmaps are the default**, one atlas entry per glyph per pixel size, which keeps small text crisp (Skia's "99.99% case"). **A font may opt into FreeType's single-channel SDF render mode**, one atlas for every size, for world-space, scaled and animated text, at the cost of hinting at small sizes and rounded corners at large ones. The opt-in is per face as a font family registers it (or `@font-face` declares it), so a UI mixes both, and one font asset may be registered in both modes.
- **Paragraph layout lives in FenrirText**, not the painter: runs of mixed style, wrapping to a width, alignment, line metrics, hit testing from a point to a character and back, as SkParagraph and Godot's TextParagraph do. The painter draws laid-out glyph runs as textured quads (ADR-0060).
- **`DebugFont` stays as it is.** The renderer's screen text does not depend on FenrirText; moving it is a later cleanup.

## Not yet

- **Multi-channel SDF through msdfgen**, which keeps sharp corners at large sizes. Taken when a scene shows FreeType's SDF corners are not enough; it adds a dependency and a second atlas format.
- **LCD subpixel rendering**, which needs the text to be unscaled and axis-aligned on an opaque background; UI over a game rarely is.
- **Colour emoji** (CBDT, COLR), **variable fonts** and **system font fallback**.

## Considered

- **stb_truetype throughout**, as the debug font does. No hinting, no shaping, and its own header warns it does no range checking of untrusted files; a game ships fonts it does not control.
- **ICU** for bidi and breaking. Correct and complete, and larger than the rest of the engine's dependencies together.
- **MSDF for everything** (Unity's and Godot's option). Scale-independent and outlines for free, but small text loses hinting, which is where most UI text is.
- **GPU glyphs from outlines** (Slug, s&box's GPU text). No atlas and exact at every size, but a per-pixel curve scan in the painter's shader for the most common thing it draws, and in s&box it landed five years after the rest of the UI.
