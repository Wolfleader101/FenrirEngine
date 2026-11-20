# 03: Sub-emitters, events and update callbacks

Spec: [Particles](../spec.md). ADR-0089.

**What to build:** A particle's birth, death or collision can fire another effect as a sub-emitter. CPU effects send `ParticleBirth`, `ParticleDeath` and `ParticleCollision` events (emitter, position, velocity) through engine event queues to C++ and C# readers, and may take an update callback from C++ or C# that sees the effect's particles after the modules run. ParticleDemo gains a Fireworks scene.

**Blocked by:** 02
**Touches:** `packages/FenrirRenderer/Scripting/`, `packages/FenrirRenderer/Managed/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/api/`, `packages/FenrirAssets/`, `examples/ParticleDemo/`

**Status:** ready-for-agent

Stacked on 10. On the core stack 01, 02, 09, 10, 03, 04, 05, 06, 11, 12: CPU bursts, then GPU simulation, then the rest, as decided.

- [ ] CPU simulation tests: sub-emitters fire on birth and death; events reach a reader the same frame; the callback sees and can move particles.
- [ ] ParticleDemo's Fireworks scene works by hand; smoke runs pass.
- [ ] FenrirRendererScripting fixture tests: events read in C#; a C# update callback moves particles.
