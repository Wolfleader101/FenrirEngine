# No particles or billboards

Status: needs-triage
Blocked by: none

## What is missing

The renderer has no particle systems, billboards, trails or decals. The nearest thing is an `InstanceList` the game rewrites each frame.

## Who needs it

Flightless Frenzy ([map](../../flightless-frenzy/map.md)): feedback for collecting a coin, a cage opening, a power-up starting and ending, a stomp, dust on landing. The design document also wanted weather from particles; that is not in the first spec.

## Does anything plan it?

Barely: particles are one line under "Not yet specified" on the renderer map (`.scratch/fenrir-renderer/map.md:79`). Sprites are out of that map's scope; trails and decals have no plan.

## Does it block the game?

Yes: it must be resolved before the game is complete (the user, 2026-11-19). The game's effects come from the engine, not from instanced meshes the game moves itself. It does not block the first playable slice, which can run with no effects.

## Comments
