# Does the engine offer input actions or dead zones?

Type: grilling
Status: open
Blocked by: none
Settles: [No input actions or bindings](09-input-actions.md)

## Question

FenrirInput reports devices and names an action layer, rebinding and dead zones as not here yet in its README; the renderer and physics maps rule input mapping out of their scope. Flightless Frenzy wants the same actions from keyboard and mouse and from a gamepad, with sensible stick dead zones; rebinding is not wanted ([What are the HUD and the menus?](../../flightless-frenzy/issues/13-hud-and-menus.md) chose no key rebinding).

- Does an action layer belong in the engine at all, by the engine/game line, or is it the game's?
- Do stick dead zones (radial, per stick, configurable) belong in FenrirInput even if actions do not?

Outcome: a spec-ready design, or `wontfix` with the reason, for each half.
