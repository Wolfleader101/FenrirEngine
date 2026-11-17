# 06: The `inset` and `border-image` shorthands, and `currentcolor`

**What to build:** two shorthands and the `currentcolor` colour, with CSS's initial border and shadow colours (spec: "Shorthands and `currentcolor`").

- **`inset`:** one to four values into `top`, `right`, `bottom`, `left`, through `ExpandSides`.
- **`border-image`:** `<source> [<slice>{1,4}]` into `border-image-source` and `border-image-slice`; a `/`, an outset or a repeat keyword is a logged error naming the two longhands FenrirUI has.
- **`currentcolor`:** a colour in every colour-taking property and function the spec lists, kept through the cascade and resolved to the element's own `color` after it, so it follows `color`'s transitions; `color: currentcolor` is `inherit`. Transitions between colours mix the resolved colours.
- **Initial values:** the border colours and a shadow's, text shadow's and drop shadow's colour left out are `currentcolor`; the `border` shorthand's colour left out too (the comment in `ExpandBorder` and the README's "a colour left out is black: FenrirUI has no `currentcolor`" go).
- **Existing sheets:** the default stylesheet and UIDemo's `hud.css`, `menu.css` and `world.css` are read for borders and shadows that relied on black, and given `black` where that is what they want, so they draw as before.
- **Docs:** the README's shorthand list and values gain them.
- **UIDemo:** `world.css` lays the terminal's backing out with `inset: 0` and gives its swatch a `currentcolor` border following the reading's colour; the name plate's frame uses the `border-image` shorthand if it has one, else a frame from the demo assets. The README says so.

**Blocked by:** 01 (One CaptureTarget and test cleanups).

**Touches:** `packages/FenrirUI/src/` (`StyleValues.cpp`, `Cascade.cpp`, `Style.cpp`, `StyleAnimation.cpp`), `packages/FenrirUI/include/FenrirUI/Style.hpp`, `packages/FenrirUI/Resources/default.css`, `packages/FenrirUI/Tests/`, `packages/FenrirUI/README.md`, `examples/UIDemo/assets/ui/`, `examples/UIDemo/README.md`

**Status:** resolved

- [x] `inset: 1px 2px` resolves top and bottom to 1, left and right to 2; `inset: 1px 2px 3px 4px 5px` is a logged error.
- [x] `border-image: url(frame.png) 12` resolves as the two longhands do; `border-image: url(frame.png) 12 / 4px` is a logged error naming them.
- [x] `color: red; border: 2px solid` gives red borders; `border-color: currentcolor` on a child with `color: blue` is blue; a `box-shadow` with no colour is the text colour.
- [x] While `color` transitions from red to blue, a `currentcolor` border is the same colour as the text each frame.
- [x] `CURRENTCOLOR` parses; `color: currentcolor` takes the parent's colour.
- [x] The default stylesheet's controls and UIDemo's roots draw as before (the existing painting and UIDemo tests pass).

Hand check: in UIDemo the terminal's swatch border follows the reading's colour (the reading now takes the lamps' colour too, so the two change together; the swatch's fill is that colour half seen, so its border shows), and the name plate has its glowing frame.
