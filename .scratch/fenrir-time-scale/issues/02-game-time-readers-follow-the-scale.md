# 02: Game-time readers follow the scale

Spec: [Pause and time scale](../spec.md). ADR-0086.

**What to build:** Everything that is game time follows the scale and pause: physics and character bodies through the fixed tick (their interpolation holding while paused), skeletons' tick and frame posing, animation playback, `Animator` crossfades (which ignore the animator's own speed but follow the scale, freezing while paused), and debug-draw durations in seconds. Most already read `deltaTime` and the accumulator; this ticket proves each with tests and fixes any that read real time.

**Blocked by:** 01
**Touches:** `packages/FenrirPhysics/`, `packages/FenrirSkeleton/`, `packages/FenrirAnimation/`, `packages/FenrirDebugDraw/`

**Status:** ready-for-agent

Beside 03, 04 and 05, on 01.

- [ ] FenrirPhysics: a falling body moves a quarter as far per frame at 0.25, and its interpolated pose holds while paused.
- [ ] FenrirSkeleton: tick and frame posing follow the scale.
- [ ] FenrirAnimation's playback tests: playback advances by scaled time; a crossfade in progress does not advance while paused.
- [ ] FenrirDebugDraw's lifetime tests: a two-second shape counts scaled time and stays while paused.
