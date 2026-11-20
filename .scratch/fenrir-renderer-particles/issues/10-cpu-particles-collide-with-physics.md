# 10: CPU particles collide with physics

Spec: [Particles](../spec.md). ADR-0089.

**What to build:** A glue package between FenrirRenderer and FenrirPhysics, `FenrirParticlePhysics`, gives CPU effects physics collision: ray casts against the active scene's physics world per moving particle, bouncing, sliding, losing life or dying as the collision module says, and sending `ParticleCollision` events and collision sub-emitters. ParticleDemo installs physics and gains a Collision scene of debris landing on a floor and steps.

**Blocked by:** 05
**Touches:** `packages/FenrirParticlePhysics/` (new), `CMakeLists.txt`, `examples/ParticleDemo/`

**Status:** ready-for-agent

Stacked on 09, on the one stack 01 to 12, in build order as decided: CPU bursts and C# bursts, then GPU simulation, then sub-emitters and events, ribbons, mesh particles, lit and light-emitting particles, and collision last.

- [ ] Glue tests: CPU particles hitting a static collider bounce, slide or die as configured, and send collision events.
- [ ] The glue package builds only with physics and the renderer on.
- [ ] ParticleDemo's Collision scene works by hand; smoke runs pass.
- [ ] A collision sub-emitter fires at the hit point (sub-emitters land in ticket 05).
