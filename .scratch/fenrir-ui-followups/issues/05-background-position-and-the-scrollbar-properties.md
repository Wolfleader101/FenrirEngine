# 05: `background-position`, `scrollbar-width` and `scrollbar-color` style what they name

**What to build:** the last three longhands the spec's property set lists and FenrirUI parses and drops (spec: "The remaining longhands"), so no property is parsed and ignored.

- **`background-position`:** a styled property (one or two keywords, lengths or percentages, CSS's meaning; initial `0% 0%`), with a setter and transitions as two lengths; box painting places a `cover` or `contain` background image by it; `stretch` and gradients ignore it. The `background` shorthand's position, written today and read by nothing, now lands.
- **`scrollbar-width`** (`auto`, `thin`, `none`) and **`scrollbar-color`** (`auto`, or thumb then track): styled properties of a scroll view, not inherited, with setters. Where not `auto` they win over the `::scrollbar` and `::scrollbar-thumb` widths and background colours: `thin` is half the default stylesheet's bar width, `none` hides the bars (no track or thumb drawn or hit) while the view still scrolls by wheel and keys. `GetScrollbars` reports the bars as drawn.
- **`LaterLonghands`** and `PropertyKind::NotYetStyled` go.
- **Docs:** the README's "Values" sentence naming the ignored properties goes; the visual list and the scroll view paragraph gain the three.
- **UIDemo:** `menu.css` gives the controls list `scrollbar-width: thin` and a `scrollbar-color`, and the pause panel a corner-placed `contain` background image from the shared demo assets. The README says so.

**Blocked by:** 01 (One CaptureTarget and test cleanups).

**Touches:** `packages/FenrirUI/src/` (`StyleValues.cpp`, `Style.cpp`, `StyleAnimation.cpp`, `BoxPainting.cpp`, `Scrolling.cpp`), `packages/FenrirUI/include/FenrirUI/Style.hpp`, `packages/FenrirUI/Tests/`, `packages/FenrirUI/README.md`, `examples/UIDemo/assets/ui/menu.css`, `examples/UIDemo/README.md`

**Status:** resolved

- [x] A 100 by 50 image at `contain` in a 200 by 200 box, scaled to 200 by 100, draws at y 50 with `center`, 0 with `top` and 100 with `bottom` (worked from CSS; the 75 and 150 first written here were the unscaled image's); at `cover`, `right` crops its left.
- [x] `background: url(x.png) right / contain` and the longhand draw the same.
- [x] `scrollbar-width: thin` gives a track half the default width; `none` gives no bars in `GetScrollbars` and no bar draws, and the wheel still scrolls.
- [x] `scrollbar-color: red blue` draws a red thumb and a blue track over a `::scrollbar-thumb` rule's green; `auto` leaves the rule's green.
- [x] A stylesheet using none of the three draws exactly as before, but for a `cover` or `contain` background image, which was centred and now sits at CSS's initial `0% 0%`, the top-left.

Hand check: in UIDemo the controls list has a thin coloured bar and the pause panel its corner image (on the panel's title, at its right end: a `contain` image over the whole card would fill its height).
