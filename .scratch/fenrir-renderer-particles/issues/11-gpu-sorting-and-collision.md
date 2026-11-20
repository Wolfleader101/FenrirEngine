# 11: GPU sorting and collision

Spec: [Particles](../spec.md). ADR-0089.

**What to build:** GPU alpha particles are sorted back to front per emitter per view by a bitonic sort in compute, and GPU effects collide with the view's scene depth and with planes, boxes and spheres placed in the scene, per the collision module.

**Blocked by:** 04
**Touches:** `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/api/`, `packages/FenrirAssets/`, `examples/ParticleDemo/`

**Status:** ready-for-agent

Stacked on 10, on the one stack 01 to 12, in build order as decided: CPU bursts and C# bursts, then GPU simulation, then sub-emitters and events, ribbons, mesh particles, lit and light-emitting particles, and collision last.

- [ ] A GL test: a GPU emitter's particles come out of the sort in back-to-front order.
- [ ] A GL test: GPU particles stop at a placed plane and at scene depth as configured.
- [ ] The CPU and GPU agreement test covers collision with shapes.
- [ ] The API description and generated C# bindings are current with this ticket's modules.
