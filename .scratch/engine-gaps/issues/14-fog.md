# No fog

Status: needs-triage
Blocked by: none

## What is missing

The renderer has no fog of any kind: no fade of far geometry toward a horizon or sky colour, by distance or by height.

## Who needs it

Flightless Frenzy ([map](../../flightless-frenzy/map.md)): its one level is open and about 660 units wide, and its far edges need to soften into the sky ([What does the game look like?](../../flightless-frenzy/issues/12-the-look.md)).

## Does anything plan it?

Only as a line: "fog and volumetric light" sits under "Not yet specified" on the renderer map (`.scratch/fenrir-renderer/map.md:72`), never triaged. The game needs the plain kind, not volumetric light.

## Does it block the game?

Yes: it must be resolved before the game is complete (the user, 2026-11-19). The game does not fake it inside its own material shader. It does not block the first playable slice.

## Comments
