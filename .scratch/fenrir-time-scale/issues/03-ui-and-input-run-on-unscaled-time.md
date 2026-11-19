# 03: UI and input run on unscaled time

Spec: [Pause and time scale](../spec.md). ADR-0074.

**What to build:** What must keep running while the game is paused reads the unscaled delta: FenrirUI's motion and transitions, the cursor blink, hot-reload polling and the virtual cursor, Razor's dispatcher and timers, and input's timing. A pause menu animates and responds while the level behind it is frozen.

**Blocked by:** 01
**Touches:** `packages/FenrirUI/`, `packages/FenrirInput/`

**Status:** ready-for-agent

Beside 02, 04 and 05, on 01.

- [ ] FenrirUI tests: with the app paused, a transition advances, the cursor blink advances and the virtual cursor moves.
- [ ] A Razor timer or handler still runs while paused.
- [ ] Input edges and repeats still arrive while paused.
- [ ] With the app unpaused at scale 1, UI behaviour is unchanged.
