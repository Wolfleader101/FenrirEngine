# 03: Sub-emitters, events and update callbacks

Spec: [Particles](../spec.md). ADR-0089.

**What to build:** A particle's birth, death or collision can fire another effect as a sub-emitter. CPU effects send `ParticleBirth`, `ParticleDeath` and `ParticleCollision` events (emitter, position, velocity) through engine event queues to C++ readers, and may take a C++ update callback that sees the effect's particles after the modules run. ParticleDemo gains a Fireworks scene.

**Blocked by:** 02
**Touches:** `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/api/`, `packages/FenrirAssets/`, `examples/ParticleDemo/`

**Status:** ready-for-agent

Stacked on 02. On the core stack 01, 02, 03, 04, 05, 06, 09, 10, 11, 12.

- [ ] CPU simulation tests: sub-emitters fire on birth and death; events reach a reader the same frame; the callback sees and can move particles.
- [ ] ParticleDemo's Fireworks scene works by hand; smoke runs pass.
