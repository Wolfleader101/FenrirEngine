# 05: Particles spawn decals

Spec: [Decals](../spec.md).

**What to build:** A particle effect's sub-emitter can be a decal (a decal material and size): at a particle's death or collision point it spawns an ordinary `Decal` entity under the cap, oriented by the particle's velocity or the hit normal. LightingDemo's Decals scene gains a burst that leaves stains where it lands.

**Blocked by:** 02, 04
**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `examples/LightingDemo/`

**Status:** ready-for-agent

Stacked on 04. Needs the particles spec's sub-emitters (`.scratch/fenrir-renderer-particles/issues/05`) built first.

- [ ] CPU simulation tests: a decal sub-emitter spawns a `Decal` at the death or collision point with the expected orientation.
- [ ] The Decals scene's burst leaves stains by hand; smoke runs pass.
