# 14: Gamepad navigation and the virtual cursor

**What to build:** a menu is fully usable on a controller: the stick or D-pad moves focus between controls, a button activates, and an optional cursor driven by the stick works pointer-driven screens (spec: "FenrirUI" input and focus; ADR-0065).

- **Directional navigation**: when a root has focus and a gamepad direction input arrives (D-pad, or the left stick past a dead zone with repeat), focus moves to the focusable element whose rectangle is nearest in that direction (Unity's and Godot's rule: in the direction's half-plane, scored by distance along the axis and overlap across it); scroll views scroll to reveal the newly focused element; the first focusable element takes focus when a root with none receives a direction; Escape-equivalent (B) sends a cancel kind of `UIEvent`; A activates as Enter does. Routers consume gamepad input only while a root has focus and publishes the gamepad claim.
- **Focus ring**: `:focus` styling in the default stylesheet is visible; `focus-visible`-like behaviour is not distinguished.
- **Virtual cursor**: a per-root switch that turns stick motion into pointer motion at a configurable speed and a button into press and release, drawn as a cursor element above everything by the sheet.
- Explicit focus neighbours (`focus-up` and friends) are not in this ticket.

**Blocked by:** 11, 12

**Touches:** `packages/FenrirUI/`

**Status:** ready-for-agent

- [ ] Headless App tests with the test feed's gamepad: a grid of buttons navigates right, down, left and up to the geometrically right neighbours, including an offset row; a direction with no candidate keeps focus; A clicks the focused button; B sends cancel; a focused element inside a scroll view scrolls into view; a root without focus takes it on the first direction; the gamepad claim is published while the UI has focus and not otherwise.
- [ ] Virtual cursor tests: stick motion moves the cursor and hover follows it; a button press clicks under it; with the switch off nothing moves.
- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
