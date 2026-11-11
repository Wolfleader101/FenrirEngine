# 12: Controls and the default stylesheet

**What to build:** buttons, checkboxes, sliders, progress bars, images and scroll views that work with the mouse and keyboard and look usable with no stylesheet of the game's (spec: "FenrirUI" controls, default stylesheet; ADR-0063's line).

- **Button**: text through the label path; a press and release inside its bounds is a click; Enter and Space when focused; `disabled` with `:disabled`.
- **Checkbox**: `checked` property, toggled by click and Space, `:checked`, sends change.
- **Slider**: min, max, step, value; drag on its track, arrow keys, wheel; sends change while dragging and on release; horizontal and vertical.
- **Progress**: min, max, value; a fill child sized by the fraction.
- **Image**: a source image asset and a fit (cover, contain, stretch), measured by its size.
- **Scroll view**: vertical and horizontal, `overflow: scroll`; wheel, scrollbar drag, content drag past a threshold, keyboard when focused; scrollbars as pseudo-elements styled by the sheet; the scroll offset readable and settable; children clipped and hit-tested with the offset.
- **The default stylesheet**: embedded in the package, neutral greys, one sans family resolved to the embedded face, hover, active, focus-ring, checked and disabled states for every control, applied to every root under the game's sheets.
- Each control is an `Element` subclass registered under its tag name.

**Blocked by:** 09, 11

**Touches:** `packages/FenrirUI/`

**Status:** ready-for-agent

- [ ] Headless App tests with the test feed: a button clicked sends click and not when the release is outside; Space on a focused button clicks; a checkbox toggles and reports `:checked` and change; a slider dragged to its middle reports the mid value snapped to its step, arrows move by a step; a progress bar's fill rectangle is the fraction of its track; an image measures its asset's size under each fit; a scroll view scrolls by the wheel, clamps at its content's end, drags its bar, and hit-tests a child at its scrolled position.
- [ ] Default stylesheet tests: every control resolves a background, text colour and focus ring from the embedded sheet with no game sheet; a game sheet overrides it by specificity.
- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
