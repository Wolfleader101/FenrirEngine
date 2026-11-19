# Does the engine offer input actions or dead zones?

Type: grilling
Status: resolved
Blocked by: none
Settles: [No input actions or bindings](09-input-actions.md)

## Question

FenrirInput reports devices and names an action layer, rebinding and dead zones as not here yet in its README; the renderer and physics maps rule input mapping out of their scope. Flightless Frenzy wants the same actions from keyboard and mouse and from a gamepad, with sensible stick dead zones; rebinding is not wanted ([What are the HUD and the menus?](../../flightless-frenzy/issues/13-hud-and-menus.md) chose no key rebinding).

- Does an action layer belong in the engine at all, by the engine/game line, or is it the game's?
- Do stick dead zones (radial, per stick, configurable) belong in FenrirInput even if actions do not?

Outcome: a spec-ready design, or `wontfix` with the reason, for each half.

## Facts (checked 2026-11-19)

- FenrirInput clamps axes to -1..1 and applies no dead zone (`packages/FenrirWindow/src/SdlInput.cpp:129-132`); `packages/FenrirInput/README.md:91` rules actions and bindings out. FenrirUI has its own navigation dead zone of 0.5 (`packages/FenrirUI/include/FenrirUI/UIRoot.hpp:195`).

## Answer

Grilled 2026-11-19, round 1 of charting [Engine gaps for Flightless Frenzy](../map.md); the recommendation was accepted.

**Stick dead zones: in FenrirInput, spec-ready.**

- FenrirInput applies a dead zone to each gamepad stick before any reader sees its axes: the `Gamepads` resource, C#, and the UI's own navigation alike.
- **Radial**, per stick: the stick's (x, y) length below the inner value reads as zero; above it the length is rescaled from the inner value to 1 so there is no jump, keeping the direction. An outer value (default 1) lets a worn stick reach full tilt.
- **Per stick and settable:** left and right sticks have their own inner and outer values, with an engine default (a starting value of about 0.15 inner, to tune in the spec), settable per gamepad from C++ and C#. Triggers get a single-axis threshold the same way.
- **Raw values stay readable** for a game that wants its own shaping.
- FenrirUI's `UIRoot::NavigationDeadZone` (0.5) is a direction threshold for navigation, not a dead zone, and stays; it reads the dead-zoned stick.

**The action layer: deferred.** Naming actions ("jump", "move") and binding them to several devices is a building block on the engine's side of the line (which actions exist stays the game's), but no client needs it now: Flightless Frenzy reads keyboard, mouse and gamepad and merges them in its own code, and wants no rebinding. It comes back when a game wants rebinding or many bindings per action. FenrirInput's README already lists it as not here yet.
