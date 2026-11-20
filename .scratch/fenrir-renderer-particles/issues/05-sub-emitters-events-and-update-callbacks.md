# 05: Sub-emitters, events and update callbacks

Spec: [Particles](../spec.md). ADR-0089.

**What to build:** A particle's birth, death or collision can fire another effect as a sub-emitter. CPU effects send `ParticleBirth`, `ParticleDeath` and `ParticleCollision` events (emitter, position, velocity) through engine event queues to C++ and C# readers, and may take an update callback from C++ or C# that sees the effect's particles after the modules run. ParticleDemo gains a Fireworks scene.

**Blocked by:** 02
**Touches:** `packages/FenrirRenderer/Scripting/`, `packages/FenrirRenderer/Managed/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/api/`, `packages/FenrirAssets/`, `examples/ParticleDemo/`

**Status:** ready-for-agent

Stacked on 04, on the one stack 01 to 12, in build order as decided: CPU bursts and C# bursts, then GPU simulation, then sub-emitters and events, ribbons, mesh particles, lit and light-emitting particles, and collision last.

- [ ] CPU simulation tests: sub-emitters fire on birth and death; events reach a reader the same frame; the callback sees and can move particles.
- [ ] ParticleDemo's Fireworks scene works by hand; smoke runs pass.
- [ ] FenrirRendererScripting fixture tests: events read in C#; a C# update callback moves particles.
