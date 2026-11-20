# 01: Camera-facing billboards in instanced drawing

Spec: [Particles](../spec.md). ADR-0089.

**What to build:** Instanced drawing gains a facing mode, a public primitive any game may use: each copy faces the camera, stretches along a velocity it carries, or faces a fixed axis, instead of keeping its own rotation. Shadows and the depth prepass draw faced copies the way the colour pass does.

**Blocked by:** None (can start immediately)
**Touches:** `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/api/`

**Status:** ready-for-agent

Bottom of the one stack 01 to 12, in build order as decided: CPU bursts and C# bursts, then GPU simulation, then sub-emitters and events, ribbons, mesh particles, lit and light-emitting particles, and collision last.

- [ ] Null-backend tests: a faced instance list's draw carries its facing mode; each mode reaches the vertex stage.
- [ ] A CPU test of the facing maths for the three modes.
- [ ] Existing instanced drawing is unchanged with no facing set.
