# 10: GPU sorting and collision

Spec: [Particles](../spec.md). ADR-0077.

**What to build:** GPU alpha particles are sorted back to front per emitter per view by a bitonic sort in compute, and GPU effects collide with the view's scene depth and with planes, boxes and spheres placed in the scene, per the collision module.

**Blocked by:** 09
**Touches:** `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/api/`, `packages/FenrirAssets/`, `examples/ParticleDemo/`

**Status:** ready-for-agent

Stacked on 09. On the core stack 01, 02, 03, 04, 05, 06, 09, 10, 11, 12.

- [ ] A GL test: a GPU emitter's particles come out of the sort in back-to-front order.
- [ ] A GL test: GPU particles stop at a placed plane and at scene depth as configured.
- [ ] The CPU and GPU agreement test covers collision with shapes.
