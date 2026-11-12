# 09: Labels and text style

**What to build:** an element with text measures, wraps and draws it in the style the stylesheet gives (spec: "FenrirUI" controls (label), property set (text properties); ADR-0062, ADR-0064).

- The **label** element: `text` set from code; a FenrirText paragraph kept on the element, relaid out when text, style or available width changes; Yoga's measure function returns the paragraph's size at the given constraints; painted through the painter's text draw with shadow and decoration.
- **Text properties** in the cascade and inline style, all inherited: `color`, `font-family`, `font-size`, `font-weight`, `font-style`, `text-align`, `line-height`, `letter-spacing`, `text-shadow`, `white-space` (normal, nowrap), `text-overflow` (clip, ellipsis). A font family resolves through FenrirText's registry (`@font-face` wiring is ticket 16; this ticket registers families from code and the embedded face).
- Any element may carry text the same way (buttons in ticket 12 reuse it).

**Blocked by:** 06, 08

**Touches:** `packages/FenrirUI/`

**Status:** ready-for-agent

- [ ] Headless App tests: a label's laid-out size equals its paragraph's size; wrapping follows the parent's width and `white-space: nowrap` stops it; ellipsis at a fixed width; `text-align` moves the lines; `font-size` from a parent inherits and a class override wins; a label in a flex row with `flex-grow` wraps at its share.
- [ ] Draw-list tests through a capturing target: a label's glyph quads sit inside its content box; `text-shadow` draws first.
- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

- From ticket 06 (painter text): the painter's `DrawText` takes a paragraph laid out in the target's pixels. Lay a label's paragraph out with each style's `font-size`, and the available width, multiplied by the root scale and the target's pixels per unit (`size * scale * pixelsPerUnit`), the exact pixel size its bitmaps are rasterised at (the spec's FenrirText section). Divide the paragraph's measured size by the same factor to give Yoga a size in UI units. The painter divides the glyph positions back, so each glyph's whole-pixel rectangle lands on whole pixels.
