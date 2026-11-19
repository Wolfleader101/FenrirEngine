# What are pause and time scale?

Type: grilling
Status: open
Blocked by: none
Settles: [No pause and no time scale](05-pause-and-time-scale.md)

## Question

Flightless Frenzy's pause menu freezes the level behind a blurred panel while the UI and input run ([What are the HUD and the menus?](../../flightless-frenzy/issues/13-hud-and-menus.md)); won and lost do the same; a brief slow-down for effect is possible. What does the engine offer?

- **Whose clock:** one global time scale on the app, one per scene, or both multiplied.
- **What it scales:** the fixed tick (physics, character bodies), frame-time systems, animation (ADR-0034 already names a global animation time scale as not yet specified), debug-draw durations (ADR-0025), UI transitions and Razor timers (presumably not).
- **Unscaled time:** what a system that must keep running reads (UI, menus, a camera that orbits while paused), and how a system says which clock it uses.
- **Pause as scale 0** or a separate flag; what a fixed tick does at scale 0 (no ticks, interpolation held); whether physics is stepped with a scaled step or ticks at a scaled rate.
- **The C# surface** for both.
- Whether this is an ADR (it touches every package that reads time).

Outcome: a spec-ready design.

## Facts (checked 2026-11-19)

- `Time` (`packages/FenrirTime/include/FenrirTime/Time.hpp`): `deltaTime`, `tickRate` (1/60), `accumulator`, `maxTicksPerFrame` (5), `frameCount`, `tickCount`; no scale or pause.
- `App::RunFrame` (`packages/FenrirApp/src/App.cpp:230-262`) advances time, then runs PreTick, Tick and PostTick while `accumulator >= tickRate`, dropping the remainder past the cap; `App::GetTime()` is read-only (`App.hpp:618`).
- Physics steps by `tickRate` in PostTick and blends by `accumulator / tickRate` (`packages/FenrirPhysics/src/Physics.cpp:43,66`). Skeletons pose by `tickRate` on the tick and `deltaTime` per frame (`packages/FenrirSkeleton/src/Skeletons.cpp:161,204`). `Animator` has per-entity `speed` and `playing` (`packages/FenrirAnimation/include/FenrirAnimation/Animator.hpp:46,50`); its crossfade runs on unscaled frame time (:52).
- An inactive scene is skipped because each package reads only the active scene; there is no per-scene flag. FenrirUI has its own CSS `animation-play-state` (`packages/FenrirUI/src/StyleValues.cpp:2313`).
