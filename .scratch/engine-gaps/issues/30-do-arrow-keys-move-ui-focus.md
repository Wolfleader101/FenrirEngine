# Do arrow keys move UI focus?

Type: grilling
Status: resolved
Blocked by: none
Settles: [Arrow keys do not move focus in FenrirUI](10-arrow-key-focus-in-ui.md)

## Question

FenrirUI moves focus with Tab and with a gamepad; the arrow keys do nothing. Flightless Frenzy's menus want the arrow keys (and maybe W and S) to move focus as a gamepad does.

- Do the arrow keys drive the same directional move the gamepad drives, always, or only when an element or root opts in (a text box needs its arrow keys)?
- Are W and S the game's to map (by sending the same move), or the engine's?
- How a game sends a focus move of its own, if it maps keys itself.

Outcome: a spec-ready design (likely one small ticket).

## Facts (checked 2026-11-19)

- Gamepad focus is spatial: the d-pad and the left stick past `UIRoot::NavigationDeadZone` map to directions (`packages/FenrirUI/src/InputRouting.cpp:253,271`), and `Navigate` (:959-998) offers the direction to the focused element first, then moves to the nearest (:297).
- The keyboard reaches only Tab and Shift+Tab, in tree order (`MoveFocus`, :812-835). Arrow keys are handled inside controls: the slider (`Controls.cpp:415-420`), the scroll view (:813-822) and the text field (:1247-1257). Since `Navigate` offers the direction to the focused element first, a control that uses arrows can keep them.

## Answer

Grilled 2026-11-19, round 1 of charting [Engine gaps for Flightless Frenzy](../map.md); the recommendation was accepted.

**Arrow keys drive directional navigation, always; spec-ready as one small ticket.**

- The four arrow keys, while a UI root has the keyboard focus, send the same direction the D-pad sends into `Navigate` (`packages/FenrirUI/src/InputRouting.cpp:959-998`). As today, the direction is offered to the focused element first, so a text field, slider or scroll view keeps its arrow keys; only a direction no element takes moves the focus.
- Enter or Space activates, as A does; Escape cancels, as B does, where the focused element does not take them.
- **W and S are the game's.** FenrirUI gains a public call, from C++ and C#, that sends a navigation direction (and activate or cancel) to a root, so a game maps any key or action to it. The engine binds only the arrow keys.
- The glossary's *Gamepad navigation* becomes **Directional navigation**, since it is no longer the gamepad's alone; FenrirUI's README and ADR-0065's wording follow in the spec.
