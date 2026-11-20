# 09: GPU simulation

Spec: [Particles](../spec.md). ADR-0089.

**What to build:** An effect can simulate on the GPU. Instance lists can be GPU-written (their copies and count written by a compute pass and drawn indirectly), the user pass in `Cull` ADR-0057 left for later, taken by the renderer's particle feature. Per GPU emitter, compute passes before the cull emit, simulate (spawn, initial values, forces and noise, over-life) and compact into a storage buffer and write a GPU-written instance list of billboards. Its shaders are written in Slang (ADR-0073). ParticleDemo gains a Fountain scene with a CPU or GPU switch.

**Blocked by:** 02
**Touches:** `docs/adr/0057-*.md`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/api/`, `packages/FenrirAssets/`, `examples/ParticleDemo/`

**Status:** ready-for-agent

Stacked on 02. On the core stack 01, 02, 09, 10, 03, 04, 05, 06, 11, 12: CPU bursts, then GPU simulation, then the rest, as decided.

- [ ] A GL test (label `gl`): the same effect, seed and steps on CPU and GPU, the GPU particles read back and compared with the CPU ones within a tolerance.
- [ ] Null-backend tests: GPU emitters' compute passes run before the cull; their instance lists draw indirectly with a GPU-written count.
- [ ] ParticleDemo's Fountain scene works on both by hand; smoke runs pass.
- [ ] ADR-0057's "GPU-written instance lists" not-yet is amended to point at ADR-0089.
- [ ] The API description and generated C# bindings are current with this ticket's modules.
