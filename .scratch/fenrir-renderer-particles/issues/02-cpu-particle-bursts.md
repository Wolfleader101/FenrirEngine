# 02: CPU particle bursts

Spec: [Particles](../spec.md). ADR-0077.

**What to build:** A game defines a `ParticleEffect` asset in C++ code (CPU simulation, a capacity, world or emitter space, and the spawn, initial values, forces and noise, and over-life modules, curves and gradients as small fixed tables), plays it from a `ParticleEmitter` component (playing or not, once or looping, a seed; a stopped emitter lets its particles finish) or once at a position with `Particles::Burst`. The CPU simulation runs once a frame on game time before `Render` and writes a camera-facing instance list per emitter; billboards draw with a texture or flipbook, additive or alpha, alpha particles sorted back to front within their emitter and emitters sorted among transparents. Emitters have bounds and are culled per view. A new ParticleDemo app on the demo scaffold opens with a Bursts scene, keys for the burst's main parameters.

**Blocked by:** 01
**Touches:** `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/api/`, `packages/FenrirAssets/`, `examples/ParticleDemo/`

**Status:** ready-for-agent

Stacked on 01. On the core stack 01, 02, 03, 04, 05, 06, 09, 10, 11, 12.

- [ ] CPU simulation tests at a fixed seed and step: spawn by rate, distance and bursts; lifetimes; forces and noise; over-life curves and gradients; capacity; world and emitter space.
- [ ] Pause and the time scale stop and slow the simulation (with the time scale spec built; else at scale 1).
- [ ] Null-backend tests: billboards draw in the transparent pass, alpha sorted within the emitter, additive not; out-of-view emitters are culled.
- [ ] ParticleDemo's Bursts scene works by hand and its smoke runs pass.
