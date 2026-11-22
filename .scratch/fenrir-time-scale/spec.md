# Pause and time scale

Status: ready-for-agent

Charted by [Engine gaps for Flightless Frenzy](../engine-gaps/map.md): [What are pause and time scale?](../engine-gaps/issues/21-what-pause-and-time-scale-are.md). Settles [No pause and no time scale](../engine-gaps/issues/05-pause-and-time-scale.md). ADR-0086.

## Problem Statement

A game cannot stop or slow its world. `Time` has a frame delta, a tick rate and a cap on ticks per frame, and nothing that holds the fixed tick, physics and animation still while the UI and input keep running. The only way to freeze a level today is to switch to another scene, which loses the frozen level from view. Flightless Frenzy's pause menu shows the level frozen and blurred behind the panel, its won and lost screens hold the level still, and it may want a brief slow-down for effect; a game-side paused flag would stop only what the game wrote, while physics and animation ran on.

## Solution

The app gains one global **time scale** and a separate **pause** flag. The scale feeds the fixed tick's accumulator, so the fixed step stays the same and the simulation behaves the same at any scale; pause makes the effective scale 0 without forgetting the scale that was set. Everything that is game time follows them: the fixed tick, frame delta time, skeletons and animation, debug-draw durations and the time material shaders read. Everything that must keep running reads **unscaled time**: FenrirUI and Razor, input, the renderer's temporal effects, and any system that asks for it. C# reaches all of it, and every demo app gains keys to pause and to cycle slow motion.

## User Stories

1. As a game developer, I want to pause the game with one flag, so that the level freezes behind my pause menu.
2. As a game developer, I want resuming to restore the time scale that was set, so that a menu opened during slow motion returns to slow motion.
3. As a game developer, I want to set a time scale between 0 and some upper limit, so that I can slow the game for effect or speed it up for testing.
4. As a game developer, I want the physics step to stay fixed at every scale, so that my character and collisions behave the same in slow motion.
5. As a game developer, I want slow-motion physics to look smooth, so that fewer ticks a second do not show as stutter.
6. As a game developer, I want no fixed ticks to run while paused, so that nothing in the simulation moves.
7. As a game developer, I want frame systems to keep running while paused, reading a frame delta of zero, so that my menu logic and UI keep working.
8. As a game developer, I want an unscaled frame delta, so that a camera orbiting the paused level or a menu animation keeps moving.
9. As a game developer, I want skeletons and animation to slow with the scale and stop while paused, so that a frozen Kiki stays frozen.
10. As a game developer, I want animation crossfades to freeze while paused, so that a blend in progress does not finish behind the menu.
11. As a game developer, I want FenrirUI's transitions, motion, cursor blink and virtual cursor to keep running while paused, so that my menus animate and respond.
12. As a game developer, I want Razor panels to keep running while paused, so that my pause menu is live.
13. As a game developer, I want input to keep working while paused, so that the player can unpause and use the menu.
14. As a game developer, I want debug-draw durations to count game time, so that a debug line drawn for two seconds stays while paused.
15. As a game developer, I want material shaders' time to follow game time, so that animated materials freeze while paused and slow in slow motion.
16. As a game developer, I want an unscaled time available to material shaders too, so that something like a UI-style shimmer in the world keeps moving.
17. As a game developer, I want the renderer's temporal effects (scene history, reflections' history) to run on real frames, so that pausing does not smear or freeze them.
18. As a game developer, I want blur and post-processing behind a menu to work while paused, so that the level shows blurred behind my pause panel.
19. As a C# game developer, I want `Time.Scale`, `Time.Paused` and `Time.UnscaledDeltaTime`, so that my pause menu and slow-down are written in C#.
20. As a C# game developer, I want `Time.DeltaTime` to follow the scale and pause, so that my systems stop with the game without checking a flag.
21. As an engine maintainer, I want one place that turns real time into game time, so that no package invents its own scale.
22. As an engine maintainer, I want every demo app to have keys that pause and cycle slow motion, so that I can see what freezes and what keeps running in any demo.
23. As an engine maintainer, I want the time scale and pause recorded in an ADR, so that a later reader knows why the step stays fixed and why pause is its own flag.
24. As the Flightless Frenzy developer, I want the level frozen behind my pause, won and lost screens with my menus live, so that the game's menus work as designed.

## Implementation Decisions

- **Time.** `Time` gains a time scale (default 1, not negative), a paused flag (default false) and the frame's unscaled delta, set by the app each frame before any phase. The effective scale is 0 while paused, else the time scale. `deltaTime` becomes the unscaled delta times the effective scale. The fixed-tick accumulator advances by the scaled delta; `tickRate` and `maxTicksPerFrame` are unchanged, and the cap still drops the remainder past it. The frame count always advances; the tick count advances only when a tick runs. The app sets the scale and the flag through public calls, from C++ and C#.
- **What follows game time:** the fixed tick (physics, character bodies and every tick-phase system), `deltaTime` for frame systems, skeletons' tick and frame posing, animation playback, `Animator` crossfades (which ignore the animator's own speed but follow the scale, as their documentation already says), debug-draw durations in seconds (frame- and tick-stamped lifetimes follow their counts), and the renderer's running shader time.
- **What reads unscaled time:** FenrirUI (motion, transitions, the cursor blink, hot-reload polling, the virtual cursor), Razor's dispatcher and timers, input's timing, and the renderer's temporal effects and their history. Each such reader changes from `deltaTime` to the unscaled delta.
- **Physics interpolation** keeps blending by the accumulator's fraction of a tick; while paused the fraction stops moving, so the rendered pose holds.
- **Shaders** get the scaled time and delta as today and an unscaled time beside them in the frame's uniforms, through the shared shader includes.
- **C#:** `Time.Scale` and `Time.Paused`, readable and writable, and `Time.UnscaledDeltaTime`, read-only, beside `Time.DeltaTime` (now scaled) and `Time.TickRate`, through FenrirScripting's own bindings and the generated pipeline.
- **The demo scaffold** gains two shared keys, listed in its help with the scaffold's others: F5 pauses and resumes, F6 cycles the scale through 1, 0.5 and 0.25. They are on no smoke run's keys. If a scene already uses one, the ticket picks the next free function key and says so.
- **Glossary:** Time scale, Pause and Unscaled time are already defined.
- **ADR-0086** records the decision; ADR-0034's and ADR-0025's "not yet" notes on a time scale point to it.

## Testing Decisions

- **A good test here drives time and checks what moved**: how many ticks ran, what `deltaTime` read, where a body, a pose or a cursor ended up. It never checks how a package reads the clock.
- **FenrirApp's frame tests** (prior art: the existing frame and phase tests driving the app with `RunFrame` and a chosen frame time): the scale feeds the accumulator at a fixed step (at 0.25, a quarter of the ticks); pause runs no ticks and reads `deltaTime` as 0 while frame systems still run; resuming restores the scale; the unscaled delta keeps running; the tick cap still holds.
- **Each reader's tests:**
  - FenrirPhysics: a body falls a quarter as far per frame at 0.25, and its interpolated pose holds while paused.
  - FenrirAnimation's playback tests: playback advances by scaled time; a crossfade in progress freezes while paused.
  - FenrirSkeleton: tick and frame posing follow the scale.
  - FenrirDebugDraw's lifetime tests: a duration in seconds counts scaled time and holds while paused.
  - FenrirUI's tests: a transition, the cursor blink and the virtual cursor advance while paused.
  - FenrirRenderer: the shader time follows game time and the unscaled time does not (null-backend tests over the frame's uniforms).
- **FenrirScripting's fixture tests:** `Time.Scale` and `Time.Paused` written from C# reach the app; `Time.DeltaTime` and `Time.UnscaledDeltaTime` read what the app holds.
- **By hand, in every demo app:** F5 and F6 show the world freezing and slowing while the demo UI kit, the help and the free-fly camera's UI keep working. The demo smoke runs keep passing (no smoke run presses them).

## Out of Scope

- A per-scene time scale, and several scenes simulating at once.
- Scaling the fixed step itself.
- Per-system choice of clock beyond reading `deltaTime` or the unscaled delta.
- Slowing the game's own timers (Flightless Frenzy's slowed-timer power-up is its own clock).
- Pausing audio: there is no audio.
- A pause menu, blur or any game UI: the game's own.

## Further Notes

- The facts behind it, with file and line, are in [What are pause and time scale?](../engine-gaps/issues/21-what-pause-and-time-scale-are.md).
- Build and test only `msvc-x64-debug` locally, through the MSVC script and, in a worktree, the short-path junction (`README.md`, "Building and testing").
- ADR-0086 is `proposed` until ticket 06, the spec's last, makes it `accepted` (CODING_STANDARDS.md).
