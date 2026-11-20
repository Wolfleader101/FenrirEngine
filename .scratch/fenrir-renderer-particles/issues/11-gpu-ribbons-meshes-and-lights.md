# 11: GPU ribbons, meshes and lights

Spec: [Particles](../spec.md). ADR-0089.

**What to build:** GPU effects render as ribbons (strips built in compute), as meshes (a GPU-written instance list of the effect's mesh) and emit lights (appended to the light buffer from compute before clustering, under the same cap).

**Blocked by:** 04, 05, 10
**Touches:** `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/api/`, `packages/FenrirAssets/`, `examples/ParticleDemo/`

**Status:** ready-for-agent

Stacked on 10. On the core stack 01, 02, 03, 04, 05, 06, 09, 10, 11, 12.

- [ ] Null-backend tests: GPU ribbon and mesh emitters draw indirectly; GPU-emitted lights reach the light buffer under the cap.
- [ ] The CPU and GPU agreement test covers ribbons' strip order.
- [ ] ParticleDemo's Ribbons, Meshes and Lights scenes switch to GPU by hand; smoke runs pass.
