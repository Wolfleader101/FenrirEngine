# No fog

Status: ready-for-agent
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

- 2026-11-19, charting [Engine gaps for Flightless Frenzy](../map.md): settled by [What fog does the renderer offer?](26-what-fog-the-renderer-offers.md).

- 2026-11-19, round 3 of [Engine gaps for Flightless Frenzy](../map.md): plain distance and height fog are spec-ready in [What fog does the renderer offer?](26-what-fog-the-renderer-offers.md); volumetric fog, which the user wants explored, is [What volumetric fog does the renderer offer?](35-what-volumetric-fog-the-renderer-offers.md). The status changes when that resolves.
- 2026-11-19, round 5 of [Engine gaps for Flightless Frenzy](../map.md): volumetric fog is spec-ready, with an ADR, in [What volumetric fog does the renderer offer?](35-what-volumetric-fog-the-renderer-offers.md); with the plain fog, the gap is settled.
- 2026-11-19: the plain fog spec is written, [Plain fog](../../fenrir-renderer-fog/spec.md), `ready-for-agent` for `/to-tickets`.
- 2026-11-19: plain fog tickets cut, the tickets of [Plain fog](../../fenrir-renderer-fog/spec.md), `01` to `04` (stack 01, 02, 04; 03 beside from 01).
- 2026-11-19: the volumetric fog spec is written, [Volumetric fog](../../fenrir-renderer-volumetric-fog/spec.md), with ADR-0088, `ready-for-agent` for `/to-tickets`.
- 2026-11-20: volumetric fog tickets cut, the tickets of [Volumetric fog](../../fenrir-renderer-volumetric-fog/spec.md), `01` to `07` (stack 01 to 05 and 07; 06 beside from 05).
