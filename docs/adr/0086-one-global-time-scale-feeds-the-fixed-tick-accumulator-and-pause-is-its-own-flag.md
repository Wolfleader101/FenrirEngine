---
status: proposed
---

# One global time scale feeds the fixed-tick accumulator, and pause is its own flag

Until now `Time` had no way to stop or slow game time: a game could hold the world still only by switching to another scene. Every package that reads time (physics, skeletons and animation, debug draw, the renderer's shader time) now sees one app-wide **time scale** and a separate **pause** flag, while the UI, input and the renderer's temporal effects keep running on **unscaled time**. It was settled in a grilling session on 2026-11-19 for Flightless Frenzy's pause menu (`.scratch/engine-gaps/issues/21-what-pause-and-time-scale-are.md`, spec `.scratch/fenrir-time-scale/spec.md`).

## The decision

- **One scale, on the app.** There is no per-scene scale: only the active scene simulates, so a second clock would have nothing to drive.
- **The scale feeds the accumulator; the fixed step never changes.** At a scale of 0.25 the fixed tick runs a quarter as often at the same `tickRate`, smoothed by the interpolation physics already does between ticks. A game sees the same simulation at every scale.
- **Pause is a flag apart from the scale.** While paused the effective scale is 0: no fixed ticks run, `deltaTime` reads 0 and frame systems still run. Resuming restores whatever scale was set, so a menu opened during slow motion returns to slow motion.
- **Scaled:** the fixed tick, `deltaTime`, skeletons and animation (crossfades too, which ignore an `Animator`'s own speed but not the scale), debug-draw durations, and the time material shaders read.
- **Unscaled:** FenrirUI and Razor, input's timing, the renderer's temporal effects and any system that reads `unscaledDeltaTime`, such as a camera orbiting a paused level. Shaders get an unscaled time beside the scaled one.

## Considered options

- **Scaling the fixed step** (a smaller `tickRate` in slow motion). Rejected: the simulation would change with the scale, and Jolt's behaviour depends on its step.
- **A per-scene scale.** Rejected while only the active scene runs.
- **Pause as a scale of 0.** Rejected: resuming would have to remember the scale that was set, and every caller would have to agree who remembers it.

## Consequences

- ADR-0034's "no global time scale or pause yet" and ADR-0025's "durations in game time once the engine has a pause or time scale" are answered by this.
- Code that must keep moving while the game is paused reads unscaled time, which is new; anything reading `deltaTime` stops with the game.
