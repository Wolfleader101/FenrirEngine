# No pause and no time scale

Status: ready-for-agent
Blocked by: none

## What is missing

`Time` has `deltaTime`, `tickRate` and `maxTicksPerFrame`, and nothing to stop or scale them. There is no way to hold the fixed tick, physics and animation still while the UI and input keep running, short of switching to another scene (an inactive scene does not simulate). Only a global animation time scale is named as not yet specified (`docs/adr/0034-*.md`).

## Who needs it

Flightless Frenzy ([map](../../flightless-frenzy/map.md)): a pause menu, a held moment on the win and lose screens, and possibly a brief slow-down for effect. (Its slowed-timer power-up slows the game's own countdown, not the engine's clock, and does not need this.)

## Does anything plan it?

No.

## Does it block the game?

It blocks the pause menu done properly. The game could stub it by having every one of its systems check a paused flag, but physics and animation would keep running, so a stub pauses only what the game wrote.

## Comments

- 2026-11-19, charting [Engine gaps for Flightless Frenzy](../map.md): settled by [What are pause and time scale?](21-what-pause-and-time-scale-are.md).
- 2026-11-19, round 2 of [Engine gaps for Flightless Frenzy](../map.md): spec-ready, with an ADR, in [What are pause and time scale?](21-what-pause-and-time-scale-are.md).
- 2026-11-19: the spec is written, [Pause and time scale](../../fenrir-time-scale/spec.md), with ADR-0074, `ready-for-agent` for `/to-tickets`.
