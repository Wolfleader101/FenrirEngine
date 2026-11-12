# 11: Input routing, hover and focus, `UIEvent` and C++ listeners

**What to build:** elements respond to the mouse and keyboard, a HUD stays transparent to input by default, and every UI event reaches systems as an engine event (spec: "FenrirUI" input and focus, events; ADR-0065).

- **Routers**: each root registers an input router with FenrirInput at a priority the game sets; it hit-tests the pointer against elements with `pointer-events: all`, topmost first (z-index, then tree order), honouring rounded corners, transforms and overflow clips; tracks hover and pressed and flips `:hover` and `:active`; consumes a pointer input when it hit an element; sends keyboard and text inputs to the focused element and consumes them when it takes them; publishes pointer, pointer-capture (while pressed) and keyboard-focus claims.
- **Focus**: by click (the nearest focusable ancestor of the hit), by Tab and Shift+Tab in tree order over focusable elements, by code; `:focus` on the element and `:focus-within` on its ancestors; blur on click outside; a `focusable` property.
- **`UIEvent`**: a shared engine event (root, target `ElementId`, kind, pointer position in UI units, button, key, a float value, an integer value), sent once at the target for pointer enter, leave, move, press, release, click, double click, scroll, key press and release, text input, focus and blur, change, submit, document reloaded (the last three sent by later tickets).
- **Listeners**: C++ callbacks per element per kind, run on the target then bubbling to the root, with stop-propagation.

**Blocked by:** 07

**Touches:** `packages/FenrirUI/`

**Status:** resolved

- [x] Headless App tests with FenrirInput's test feed: a click on a panel with `pointer-events: none` reaches the game's `Mouse` and sends no event; with `all` it is consumed, sends press, release and click, and `Mouse` sees nothing; a click on a rounded corner's cut-off hits what is behind; a click outside an overflow-hidden parent's box misses a child that overflows; hover enter and leave as the pointer moves; the topmost of two overlapping elements wins by z-index.
- [x] Focus tests: click focuses, Tab and Shift+Tab walk the order, `:focus-within` on ancestors, key events go to the focused element and are consumed, the claims resource shows keyboard focus while focused.
- [x] Listener tests: a click runs the target's listener then the parent's; stop-propagation on the target keeps the parent's from running; the engine event is sent once regardless.
- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
