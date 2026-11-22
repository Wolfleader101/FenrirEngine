# No input actions or bindings

Status: ready-for-agent
Blocked by: none

## What is missing

FenrirInput reports keys, mouse buttons and gamepad buttons and axes. It has no layer that names an action ("jump", "move") and binds it to several devices, no rebinding, and no stick dead zones. `packages/FenrirInput/README.md` lists these as out of scope ("Not here yet").

## Who needs it

Flightless Frenzy ([map](../../flightless-frenzy/map.md)): the same actions from keyboard and mouse and from a gamepad, with sensible stick dead zones. Rebinding by the player is undecided ("What does the player play with?").

## Does anything plan it?

No; the README rules it out for now.

## Does it block the game?

No. The game can read both devices and merge them in its own code. Raised so triage can confirm it stays the game's, or decide dead zones at least belong in the engine.

## Comments

- 2026-11-19, charting [Engine gaps for Flightless Frenzy](../map.md): settled by [Does the engine offer input actions or dead zones?](29-does-the-engine-offer-input-actions.md).
- 2026-11-19, round 1 of charting [Engine gaps for Flightless Frenzy](../map.md): stick dead zones are spec-ready and the action layer is deferred, in [Does the engine offer input actions or dead zones?](29-does-the-engine-offer-input-actions.md).
- 2026-11-20: the dead-zone spec is written, [Stick and trigger dead zones](../../fenrir-input-dead-zones/spec.md), `ready-for-agent` for `/to-tickets`.
- 2026-11-20: tickets cut, the tickets of [Stick and trigger dead zones](../../fenrir-input-dead-zones/spec.md), `01` to `03` (01 first, then 02 and 03 side by side).
- 2026-11-20, from the game's [What does the player play with?](../../flightless-frenzy/issues/07-controls-and-input-devices.md): the dead zones are no longer only wanted. The game writes none of its own, so its gamepad ticket waits for that spec to be built; its first playable slice is keyboard and mouse only. The action layer stays deferred: the game merges its devices itself and wants no rebinding.
