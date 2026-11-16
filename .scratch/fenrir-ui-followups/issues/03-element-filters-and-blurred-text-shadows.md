# 03: `filter: blur()` and `drop-shadow()` paint an element and its subtree, and a text shadow's blur is drawn

**What to build:** the two filters of the spec's property set (FenrirUI spec story 31) and the text shadow's blur, through FenrirPainter's offscreen groups (spec: "Filters"; ADR-0060, ADR-0063).

- **The property:** `filter` leaves `LaterLonghands` and becomes a styled property holding a list of `blur(<length>)` and `drop-shadow(<offset-x> <offset-y> [<blur>] [<colour>])`, or `none`; a `StyleDeclarations` setter; not inherited; a bad value is a logged parse error with the declaration skipped. The drop shadow's colour left out is black until ticket 06 makes it `currentcolor`.
- **Painting** (`BoxPainting.cpp`): one offscreen group per function round the element and its subtree, the first function innermost, through `GroupFilter::Blur` and `GroupFilter::DropShadow`; bounds from `LocalBounds` grown by each filter's reach; an opacity group, when there is one, outside them. Past the painter's nesting limit a group draws flat, logged as today.
- **Animation:** transitions and keyframes mix two lists of the same functions in the same order; other pairs switch at the midpoint.
- **Text shadows:** a shadow with a blur draws its runs inside a blur group bounded by the paragraph's box at its offset, grown by its blur; one without stays a direct draw.
- **`offscreenGroups` off:** filters are ignored and text shadows are sharp.
- **Docs:** the FenrirUI README's "Values" and painting paragraphs say what `filter` and a text shadow's blur draw (the sentence that `filter` styles nothing goes), and the glossary's *offscreen group* needs no change.
- **UIDemo:** `hud.css` gives the compass and the ammo counter a `drop-shadow()`, and the hit marker's text a blurred `text-shadow` glow; the HUD root gets a class while the pause menu is open that blurs it (`filter: blur(3px)`, transitioned). The README's HUD and hand-check sections say so.

**Blocked by:** 01 (One CaptureTarget and test cleanups).

**Touches:** `packages/FenrirUI/src/` (`StyleValues.cpp`, `Style.cpp`, `StyleAnimation.cpp`, `BoxPainting.cpp`), `packages/FenrirUI/include/FenrirUI/Style.hpp`, `packages/FenrirUI/Tests/`, `packages/FenrirUI/README.md`, `examples/UIDemo/assets/ui/hud.css`, `examples/UIDemo/src/Menu.cpp`, `examples/UIDemo/README.md`

**Status:** resolved

- [x] Parsing: `blur(4px)`, `drop-shadow(2px 3px 4px red)`, a list of both and `none` give the expected lists; `blur(-1px)`, `sepia(1)` and `drop-shadow(red)` are logged errors.
- [x] A panel with a child and `filter: blur(4px)` records one begin group whose bounds cover both grown by 4, the panel's and the child's draws inside, and an end group with a blur of 4.
- [x] `filter: drop-shadow(...) blur(...)` records the drop-shadow group inside the blur group; with `opacity: 0.5` too, the opacity group is outermost.
- [x] A transition from `blur(0px)` to `blur(10px)` gives `blur(5px)` halfway (a linear timing function).
- [x] A text shadow with a blur records a blur group round its runs; one without records none.
- [x] With `UISettings::offscreenGroups` off, neither records a group.

Hand check: in UIDemo the compass and ammo counter cast shadows, the hit marker glows, and the HUD blurs in as the pause menu opens and clears as it closes.
