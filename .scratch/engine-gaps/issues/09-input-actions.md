# No input actions or bindings

Status: needs-triage
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
