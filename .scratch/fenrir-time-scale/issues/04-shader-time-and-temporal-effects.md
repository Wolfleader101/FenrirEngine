# 04: Shader time and the renderer's temporal effects

Spec: [Pause and time scale](../spec.md). ADR-0086.

**What to build:** Material shaders' running time follows game time, so animated materials freeze while paused and slow in slow motion, and an unscaled time joins it in the frame's shader inputs through the shared shader includes, for anything that must keep moving. The renderer's temporal effects (scene history, reflections' history) run on real frames, so pausing neither smears nor freezes them, and post-processing behind a menu keeps working.

**Blocked by:** 01
**Touches:** `packages/FenrirRenderer/`

**Status:** ready-for-agent

Beside 02, 03 and 05, on 01.

- [ ] Null-backend tests over the frame's uniforms: the shader time advances by scaled time and holds while paused; the unscaled time keeps advancing.
- [ ] The temporal effects' history advances on every frame while paused.
- [ ] The renderer's existing tests pass unchanged at scale 1.
