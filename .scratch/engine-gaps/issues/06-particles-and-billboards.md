# No particles or billboards

Status: ready-for-agent
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

- 2026-11-19, charting [Engine gaps for Flightless Frenzy](../map.md): settled by [How do engines split particles between the engine and the game?](24-how-engines-split-particles.md), [What is the engine's particle system?](25-what-the-particle-system-is.md).

- 2026-11-19, round 3 of [Engine gaps for Flightless Frenzy](../map.md): the user widened the first version to everything, on the GPU too; [What is the engine's particle system?](25-what-the-particle-system-is.md) stays open for round 4.
- 2026-11-19, round 4 of [Engine gaps for Flightless Frenzy](../map.md): particles are spec-ready in [What is the engine's particle system?](25-what-the-particle-system-is.md); decals are their own block in [What is a decal?](36-what-is-a-decal.md). The status changes when that resolves.
- 2026-11-19, round 5 of [Engine gaps for Flightless Frenzy](../map.md): decals are spec-ready in [What is a decal?](36-what-is-a-decal.md); with the particles, the gap is settled.
- 2026-11-20: the particles spec is written, [Particles](../../fenrir-renderer-particles/spec.md), with ADR-0089, `ready-for-agent` for `/to-tickets`.
- 2026-11-20: particle tickets cut, the tickets of [Particles](../../fenrir-renderer-particles/spec.md), `01` to `12` (one stack, 01 to 12 in build order since the review).
- 2026-11-20: the decals spec is written, [Decals](../../fenrir-renderer-decals/spec.md), `ready-for-agent` for `/to-tickets`.
- 2026-11-20: decal tickets cut, the tickets of [Decals](../../fenrir-renderer-decals/spec.md), `01` to `05` (stack 01, 02, 04, 05; 03 beside from 02).
