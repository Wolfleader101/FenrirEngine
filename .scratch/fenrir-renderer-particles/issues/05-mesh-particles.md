# 05: Mesh particles

Spec: [Particles](../spec.md). ADR-0089.

**What to build:** CPU effects can render as meshes: each particle draws the effect's mesh with its material through instanced drawing, opaque or transparent as the material says. ParticleDemo gains a Meshes scene of splinters bursting from a box.

**Blocked by:** 02
**Touches:** `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/api/`, `packages/FenrirAssets/`, `examples/ParticleDemo/`

**Status:** ready-for-agent

Stacked on 04. On the core stack 01, 02, 09, 10, 03, 04, 05, 06, 11, 12: CPU bursts, then GPU simulation, then the rest, as decided.

- [ ] Null-backend tests: a mesh emitter draws one instanced draw of its mesh and material in the pass its material belongs to.
- [ ] ParticleDemo's Meshes scene works by hand; smoke runs pass.
- [ ] The API description and generated C# bindings are current with this ticket's modules.
