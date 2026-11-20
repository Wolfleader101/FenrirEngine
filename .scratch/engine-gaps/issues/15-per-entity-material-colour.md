# One entity cannot have its own colour without its own material

Status: ready-for-agent
Blocked by: none

## What is missing

`MeshRenderer` has no tint and no per-entity material parameters. A per-copy colour exists only on an `InstanceList`. Today, giving one entity a different colour or parameter value means cloning its material.

## Who needs it

Flightless Frenzy ([map](../../flightless-frenzy/map.md)): each caged friend is the Kiki model in a different body colour ([Where do the assets come from?](../../flightless-frenzy/issues/11-where-assets-come-from.md)), and an active power-up changes how Kiki is lit ([What does the game look like?](../../flightless-frenzy/issues/12-the-look.md)).

## Does anything plan it?

Only as a line: per-entity material overrides are planned on the renderer map (`.scratch/fenrir-renderer/map.md:87`).

## Does it block the game?

Yes: it must be resolved before the game is complete (the user, 2026-11-19). It does not block the first playable slice; cloned materials work until then.

## Comments

- 2026-11-19, charting [Engine gaps for Flightless Frenzy](../map.md): settled by [How does one entity get its own material parameters?](27-how-one-entity-gets-its-own-material-parameters.md).

- 2026-11-19, round 3 of [Engine gaps for Flightless Frenzy](../map.md): spec-ready in [How does one entity get its own material parameters?](27-how-one-entity-gets-its-own-material-parameters.md).
- 2026-11-20: the spec is written, [Per-entity material values](../../fenrir-renderer-entity-values/spec.md), `ready-for-agent` for `/to-tickets`.
