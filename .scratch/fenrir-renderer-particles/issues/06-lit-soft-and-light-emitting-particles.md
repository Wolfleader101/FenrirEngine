# 06: Lit, soft and light-emitting particles

Spec: [Particles](../spec.md). ADR-0089.

**What to build:** Effects choose unlit or lit: lit particles shade through the shared lighting include (sun, its shadows, clustered lights) with a camera-facing normal for billboards and ribbons. Soft particles fade where they meet geometry, reading scene depth resolved under MSAA (reusing the game passes spec's resolve where it is built). Plain and volumetric fog reach particles through the fog helper where those specs are built. An effect's light-emission module gives a share of its particles a point light, written into the light buffer before clustering under a cap in the renderer config. ParticleDemo gains a Lights scene of embers lighting the ground.

**Blocked by:** 02
**Touches:** `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/api/`, `packages/FenrirAssets/`, `examples/ParticleDemo/`

**Status:** ready-for-agent

Stacked on 05. On the core stack 01, 02, 03, 04, 05, 06, 09, 10, 11, 12.

- [ ] Null-backend tests: lit particles bind the light clusters and shadows; soft particles bind scene depth; emitted lights reach the light buffer up to the cap and no further.
- [ ] ParticleDemo's Lights scene works by hand; smoke runs pass.
