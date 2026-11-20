# 07: CPU particles collide with physics

Spec: [Particles](../spec.md). ADR-0089.

**What to build:** A glue package between FenrirRenderer and FenrirPhysics (named in this ticket) gives CPU effects physics collision: ray casts against the active scene's physics world per moving particle, bouncing, sliding, losing life or dying as the collision module says, and sending `ParticleCollision` events and collision sub-emitters. ParticleDemo installs physics and gains a Collision scene of debris landing on a floor and steps.

**Blocked by:** 03
**Touches:** a new glue package under `packages/`, `CMakeLists.txt`, `examples/ParticleDemo/`

**Status:** ready-for-agent

Branches from 03 and runs beside the core stack; its ParticleDemo scene may need a small merge with the core stack's scenes.

- [ ] Glue tests: CPU particles hitting a static collider bounce, slide or die as configured, and send collision events.
- [ ] The glue package builds only with physics and the renderer on.
- [ ] ParticleDemo's Collision scene works by hand; smoke runs pass.
