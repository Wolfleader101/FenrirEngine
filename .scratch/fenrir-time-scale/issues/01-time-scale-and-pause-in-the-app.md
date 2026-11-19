# 01: Time scale and pause in the app

Spec: [Pause and time scale](../spec.md). ADR-0074.

**What to build:** The app has one global time scale and a pause flag. `Time` gains the scale (default 1, not negative), the paused flag (default false) and the frame's unscaled delta, set by the app each frame before any phase; the effective scale is 0 while paused, else the scale. `deltaTime` becomes the unscaled delta times the effective scale. The fixed-tick accumulator advances by the scaled delta at an unchanged `tickRate`, the cap on ticks per frame still drops the remainder, the frame count always advances and the tick count only when a tick runs. Public app calls set the scale and pause, and resuming restores the scale that was set.

**Blocked by:** None (can start immediately)
**Touches:** `packages/FenrirTime/`, `packages/FenrirApp/`

**Status:** ready-for-agent

First; 02, 03, 04 and 05 build on it side by side.

- [ ] FenrirApp frame tests, driving `RunFrame` with a chosen frame time: at a scale of 0.25 a quarter of the ticks run, each at the same `tickRate`.
- [ ] Paused: no ticks run, `deltaTime` reads 0, frame systems still run, and the unscaled delta keeps its value.
- [ ] Resuming restores a scale set before pausing.
- [ ] The tick cap still holds at a scale above 1; a negative scale is refused.
- [ ] The frame count advances every frame; the tick count only with a tick.
