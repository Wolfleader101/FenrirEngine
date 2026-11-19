# Do arrow keys move UI focus?

Type: grilling
Status: open
Blocked by: none
Settles: [Arrow keys do not move focus in FenrirUI](10-arrow-key-focus-in-ui.md)

## Question

FenrirUI moves focus with Tab and with a gamepad; the arrow keys do nothing. Flightless Frenzy's menus want the arrow keys (and maybe W and S) to move focus as a gamepad does.

- Do the arrow keys drive the same directional move the gamepad drives, always, or only when an element or root opts in (a text box needs its arrow keys)?
- Are W and S the game's to map (by sending the same move), or the engine's?
- How a game sends a focus move of its own, if it maps keys itself.

Outcome: a spec-ready design (likely one small ticket).
