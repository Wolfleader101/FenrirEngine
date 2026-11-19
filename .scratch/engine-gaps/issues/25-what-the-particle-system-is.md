# What is the engine's particle system?

Type: grilling
Status: resolved
Blocked by: 24
Settles: [No particles or billboards](06-particles-and-billboards.md)

## Question

Flightless Frenzy wants bursts when a coin is collected, a cage opens, a power-up starts and ends, an enemy is stomped and Kiki lands ([What does the game look like?](../../flightless-frenzy/issues/12-the-look.md)), from the engine and not from instanced meshes the game moves itself; weather later. With [How do engines split particles between the engine and the game?](24-how-engines-split-particles.md) in hand:

- **The engine/game line:** what an effect is (an emitter component, a data asset, a function call that spawns a burst), and which parts are the engine's block and which the game's choice.
- **The first version:** CPU or GPU simulation; billboards only, or meshes too; trails and decals in or out; collision in or out.
- **Rendering:** a pass of its own or the transparent pass; sorting; lit or unlit; soft particles (which need scene depth, shared with [How does a game draw outlines and add its own render pass?](23-how-a-game-draws-outlines-and-adds-a-pass.md)).
- **Triggering from C#:** a one-shot burst at a position or attached to an entity.
- **Authoring:** code first, or a data asset that waits on the `fenrir-serialisation` map.
- **Where it is planned:** this map, or the renderer map's "Particles" line graduated into a spec there.

Outcome: a spec-ready design.

## Comments

- 2026-11-19, round 3 of [Engine gaps for Flightless Frenzy](../map.md): accepted, an effect is a plain parameter struct defined from C++ or C# (burst count or rate, lifetime, speed and direction cone, gravity, size and colour over life, texture, blend mode), a data asset once `fenrir-serialisation` lands; played by a `ParticleEmitter` component (continuous or one-shot, following its entity) and `Particles.Burst(effect, position)`, from C++ and C#; the spec is `.scratch/fenrir-renderer-particles/`, decided here. **Changed by the user:** the first version has everything (trails, decals, mesh particles, collision, lit particles, soft particles) and can simulate on the GPU, not only the CPU. So where it simulates, which package owns it, and how each of those features is done reopen; they are round 4.

## Answer

Grilled 2026-11-19 over rounds 3 and 4 of [Engine gaps for Flightless Frenzy](../map.md), resting on [How do engines split particles between the engine and the game?](24-how-engines-split-particles.md). In round 3 the user widened the first version to everything, on the GPU too; round 4's recommendations were all accepted. **Spec-ready, with an ADR**; the spec is `.scratch/fenrir-renderer-particles/`.

- **Where it simulates:** each effect chooses CPU or GPU. CPU suits a few particles that collide with physics or that the game reads back; GPU suits many.
- **Who owns it:** FenrirRenderer, as it owns lighting, since GPU simulation, sorting, soft particles and lighting need its internals (compute in the frame, scene depth, the light clusters) and it admits no pass from another package. A small glue package to FenrirPhysics carries CPU collision, as the ragdoll glue does. (This replaces round 3's separate package.)
- **What an effect is:** a stack of optional engine-defined modules (spawn, velocity, forces and noise, collision, size, colour and rotation over life, sub-emitters, render mode), Shuriken's model, defined in code from C++ or C# and a data asset once the `fenrir-serialisation` map lands. No game-written GPU modules in the first version; a CPU effect may take an update callback from C++ or C#.
- **Playing one:** a `ParticleEmitter` component (continuous or one-shot, following its entity) and `Particles.Burst(effect, position)`, from C++ and C#.
- **Render modes:** camera-facing billboards; ribbons, joining an emitter's particles in spawn order into a camera-facing strip (a trail behind an entity is an emitter in this mode); meshes, each particle drawing a mesh and material through instanced drawing, which for GPU simulation means the GPU-written instance lists ADR-0057 left for later.
- **Collision:** GPU particles collide with scene depth and with simple shapes placed in the scene (planes, boxes, spheres); CPU particles collide with physics colliders through ray casts.
- **Lighting:** per effect, unlit or lit per pixel through `Fenrir/Lighting.glsl` (sun, shadows, clustered lights). Particles may emit lights, capped at a set number and written into the light clusters.
- **Blending and sorting:** additive and alpha; alpha-blended particles sort back to front within their emitter (a GPU sort for GPU simulation), and emitters sort among transparents as today. Soft particles fade against scene depth.
- **Sub-emitters** fire on birth, death and collision. Birth, death and collision **events** reach scripts for CPU particles only, since GPU events would need a readback.
- **Decals** are not particles: they are their own block, [What is a decal?](36-what-is-a-decal.md), which a particle may spawn.
- **The ADR** records compute simulation in the frame, GPU-written instance lists, and the CPU and GPU split; its number is taken when the spec is written.
- **Ticket order in the spec:** CPU billboards and bursts first (what Flightless Frenzy needs), then GPU simulation, then ribbons, mesh particles, lit and light-emitting particles, and collision.
