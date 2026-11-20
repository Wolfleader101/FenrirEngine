---
status: proposed
---

# Particles are renderer effects of engine modules, simulated on the CPU or the GPU per effect

The renderer had no particles: the nearest thing was an instance list a game rewrote each frame. Every engine studied (Godot, Unity's Shuriken and VFX Graph, Niagara, `bevy_hanabi`, Defold) splits particles the same way: the engine owns the runtime and a vocabulary of behaviours, the game owns the effect as data and plays it with one call. FenrirEngine follows that split. A **particle effect** is a stack of engine-defined modules, defined by the game in code (and from a data asset once files exist); the renderer runs it, on the CPU or the GPU as the effect chooses, and draws it as billboards, ribbons or meshes. It was settled in grilling sessions on 2026-11-19 (`.scratch/engine-gaps/issues/25-what-the-particle-system-is.md`, findings in `research/engine-particles`; spec `.scratch/fenrir-renderer-particles/spec.md`).

## The decision

- **In FenrirRenderer,** as lighting is. GPU simulation, per-particle sorting, soft particles, lit particles and particles that emit light all need the renderer's internals (compute in the frame, scene depth, the light clusters), and the renderer admits no pass from another package (ADR-0019, ADR-0087). CPU collision with physics colliders comes through a small glue package between FenrirRenderer and FenrirPhysics, as ragdolls glue animation to physics.
- **CPU or GPU, per effect.** CPU suits a few particles that collide with physics, send events or are read back by the game; GPU suits many. Both run the same modules with the same meaning, and a test holds them to agreeing.
- **A module stack, not a graph.** Spawn, initial values, forces and noise, change over life, collision, sub-emitters, rendering, lighting and light emission are fixed engine modules with parameters (Shuriken's model). No game-written GPU module in the first version; a CPU effect may take an update callback from C++ or C#.
- **GPU simulation in compute before the cull,** writing its particles into **GPU-written instance lists** whose copies and count the renderer draws indirectly. This is the user pass in `Cull` that ADR-0057 left for later, taken by the renderer's own particle feature.
- **Game time:** particles follow the time scale and pause (ADR-0086).
- **Decals are not particles;** they are their own block, which a particle may spawn.

## Considered options

- **A separate particles package over a public pass API.** Rejected: no such API exists, and building one for particles alone would expose the renderer's internals.
- **CPU only, or GPU only.** Rejected by the user: the first version has everything and can run on the GPU, while CPU particles keep physics collision and events.
- **An open module graph** (Niagara's). Rejected for the first version: the fixed stack covers the feature list without a compiler for graphs.

## Consequences

- ADR-0057's "GPU-written instance lists" are built here: an instance list's copies and count can be written by a GPU pass, and an indirect draw reads the count. ADR-0057 asked what that does to an asset's revision: a GPU-written list is the emitter's own GPU buffer, not an instance list asset in the store, so it has no revision and asset revisions keep their meaning.
- GPU particles send no events and are not readable by the game without a readback, which the first version does not offer.
- A particle that emits light takes a slot in the light buffer, under a cap.
