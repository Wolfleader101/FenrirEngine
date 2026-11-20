# Particles

Status: ready-for-agent

Charted by [Engine gaps for Flightless Frenzy](../engine-gaps/map.md): [How do engines split particles between the engine and the game?](../engine-gaps/issues/24-how-engines-split-particles.md) and [What is the engine's particle system?](../engine-gaps/issues/25-what-the-particle-system-is.md). Settles the particle half of [No particles or billboards](../engine-gaps/issues/06-particles-and-billboards.md); decals are [What is a decal?](../engine-gaps/issues/36-what-is-a-decal.md), a spec of their own. ADR-0089.

## Problem Statement

The renderer has no particles, billboards or trails. The nearest thing is an instance list a game rewrites every frame, which gives it neither camera-facing quads, per-particle sorting, soft edges, lighting nor any simulation; every effect would be the game's own code moving meshes, which Flightless Frenzy's rule forbids. The game wants bursts when a coin is collected, a cage opens, a power-up starts and ends, an enemy is stomped and Kiki lands, and weather later; the user wants the engine's first particle system to have everything and to run on the GPU as well as the CPU.

## Solution

A **particle effect** is a stack of engine modules with parameters (spawn, initial values, forces and noise, change over life, collision, sub-emitters, rendering, lighting, light emission), defined by a game in code from C++ or C#, kept in the asset store and shared by every emitter that plays it. Each effect simulates on the CPU or the GPU. A `ParticleEmitter` component plays an effect from its entity, continuously or once; `Particles.Burst` plays one at a position. The renderer draws particles as camera-facing billboards, ribbons or meshes, alpha-blended or additive, sorted, optionally soft against scene depth, unlit or lit by the sun and lights, and able to emit lights of their own. CPU particles collide with physics colliders through a small glue package and send events to C++ and C#; GPU particles collide with scene depth and simple shapes. Particles follow game time. A new ParticleDemo app shows each feature.

## User Stories

1. As a game developer, I want to define a particle effect in code, so that my bursts and trails are mine without a tool.
2. As a game developer, I want an effect kept as an asset that many emitters share, so that a coin's burst is defined once.
3. As a game developer, I want to play a burst at a position with one call, so that collecting a coin shows sparkles.
4. As a game developer, I want an emitter component that plays an effect from its entity and follows it, so that dust trails behind Kiki.
5. As a game developer, I want an emitter to play once or loop, and to start and stop it, so that a power-up's glow runs only while it lasts.
6. As a game developer, I want particles to simulate in world space or in their emitter's space, so that a trail stays behind while a halo moves with its owner.
7. As a game developer, I want spawning by rate, by distance moved and in timed bursts, so that effects start as I mean.
8. As a game developer, I want spawn shapes (point, sphere, box, cone, ring), so that a burst can fill a volume or a cone.
9. As a game developer, I want each particle's lifetime, speed, direction, size, rotation and colour chosen from ranges, so that a burst is not uniform.
10. As a game developer, I want gravity, drag, a constant force and noise, so that smoke curls and sparks fall.
11. As a game developer, I want size, colour, alpha, rotation and speed to change over a particle's life by curves and gradients, so that sparks fade and shrink.
12. As a game developer, I want a maximum particle count per emitter, so that an effect cannot run away.
13. As a game developer, I want to choose CPU or GPU simulation per effect, so that small effects stay cheap and big ones scale.
14. As a game developer, I want a seed per emitter, so that an effect can repeat exactly.
15. As a game developer, I want camera-facing billboards with a texture or a flipbook, so that particles look like sparks, smoke and leaves.
16. As a game developer, I want billboards that face the camera, stretch along their velocity, or face a fixed axis, so that rain streaks and ground puffs lie right.
17. As a game developer, I want ribbons joining an emitter's particles into a strip, so that a glide leaves a trail.
18. As a game developer, I want a ribbon's width and colour to change along it, so that a trail tapers and fades.
19. As a game developer, I want particles drawn as meshes with their material, so that a cage can burst into wooden splinters.
20. As a game developer, I want additive and alpha blending, so that glows add up and smoke covers.
21. As a game developer, I want alpha particles sorted back to front within their emitter, so that smoke does not show seams.
22. As a game developer, I want soft particles that fade where they meet geometry, so that smoke does not cut a hard line into the ground.
23. As a game developer, I want particles lit by the sun, its shadows and the scene's lights, or unlit, per effect, so that dust sits in the scene and sparks glow.
24. As a game developer, I want particles to be able to emit light, under a cap, so that embers light the ground around them.
25. As a game developer, I want CPU particles to collide with physics colliders, bouncing, sliding or dying, so that debris lands on the level.
26. As a game developer, I want GPU particles to collide with what the camera sees and with shapes I place, so that a fountain's drops splash on the ground.
27. As a game developer, I want sub-emitters that fire on a particle's birth, death or collision, so that a firework bursts where its rocket dies.
28. As a game developer, I want birth, death and collision events from CPU particles in C++ and C#, so that my game can react to a splinter landing.
29. As a game developer, I want a CPU effect to take an update callback, so that I can move particles in a way no module offers.
30. As a game developer, I want a particle to spawn a decal when it lands, once decals exist, so that a splash leaves a stain.
31. As a game developer, I want particles to slow with the time scale and stop when paused, so that my pause menu freezes the effects too.
32. As a game developer, I want emitters culled when out of view, so that effects off screen cost little.
33. As a C# game developer, I want to define effects, play bursts, add emitters, set update callbacks and read events from C#, so that my host stays a host.
34. As an engine maintainer, I want CPU and GPU simulations to agree on the same effect and seed, so that switching an effect's simulation does not change what it does.
35. As an engine maintainer, I want golden images of a burst, a ribbon and lit particles, so that a regression shows.
36. As an engine maintainer, I want a ParticleDemo app with a scene per feature, so that I can check each by hand and its smoke runs cover them.
37. As an engine maintainer, I want the design recorded in an ADR, so that a later reader knows why particles are in the renderer and run on both processors.

## Implementation Decisions

- **The effect** is an asset type in the asset store (`ParticleEffect`), made from code by C++ or C#, shared by handle; once the `fenrir-serialisation` map's data assets exist it can also load from a file. It holds whether it simulates on the CPU or the GPU, a capacity, whether particles live in world space or the emitter's, and its modules in a fixed order:
  - **spawn:** rate over time, rate over distance moved, timed bursts (count, time, repeats), and a shape (point, sphere, box, cone, ring, with emission from its volume or its surface);
  - **initial values:** ranges for lifetime, speed, direction (the shape's or a cone), size, rotation, angular speed and colour;
  - **forces:** gravity scale, drag, a constant force and noise (strength, frequency, scroll);
  - **over life:** curves for size, speed and rotation, and a gradient for colour and alpha;
  - **collision:** off, physics (CPU effects only: bounce, friction, life lost, die on hit), or depth and shapes (GPU effects: the camera's scene depth and planes, boxes and spheres placed in the scene);
  - **sub-emitters:** another effect fired on birth, death or collision;
  - **rendering:** billboards (texture, flipbook atlas and rate, facing: camera, velocity-stretched or a fixed axis), ribbons (width over life, colour along the strip, texture along or tiled), or meshes (a mesh and a material); a blend mode, additive or alpha; soft fade distance;
  - **lighting:** unlit or lit;
  - **light emission:** a share of particles carrying a point light with a colour, radius and intensity.
  Curves and gradients are small fixed-size tables, so CPU and GPU evaluate them alike.
- **Playing:** a `ParticleEmitter` component (an effect, playing or not, once or looping, a seed, a simulation-space choice inherited from the effect) on an entity; `Particles::Burst(effect, position, rotation)` from C++ and `Particles.Burst` from C#, which plays an effect once from a pooled transient emitter. A stopped emitter lets its living particles finish.
- **Time:** CPU simulation runs once a frame on game time (ADR-0086), in the frame phases before `Render`, so events reach readers that frame; GPU simulation runs in the frame's compute before the cull on the same game time. While paused neither advances.
- **CPU simulation** keeps each emitter's particles in plain arrays, applies the modules in order, and writes what the renderer draws: an instance list for billboards and meshes, a strip for ribbons, and the lights it emits. Events (`ParticleBirth`, `ParticleDeath`, `ParticleCollision`, each with emitter, position and velocity) go through engine event queues to C++ and C#. A CPU effect may take an update callback from C++ or C# that sees its particles' span after the modules run.
- **CPU collision with physics** lives in a glue package between FenrirRenderer and FenrirPhysics, `FenrirParticlePhysics`: ray casts against the active scene's physics world per moving particle, applying the collision module.
- **GPU simulation** is the renderer's particle feature: per GPU emitter, compute passes before the cull emit, simulate and compact into a storage buffer of particles, sort alpha particles per emitter per view (a bitonic sort), collide with the view's scene depth and the scene's collision shapes, and write a **GPU-written instance list** (copies and count) that the draw reads indirectly. GPU effects send no events and offer no readback.
- **Drawing:** billboards are a camera-facing mode of instanced drawing, a public primitive any game may use; ribbons are a strip drawn from the emitter's particles in spawn order, built on the CPU for CPU effects and in compute for GPU ones; meshes are instanced drawing with the effect's material. Alpha particles draw in the transparent pass, emitters sorted among transparents as today and particles sorted within; additive ones need no sort. Soft particles read scene depth, resolved under MSAA (reusing the game passes spec's resolve where it is built). Lit particles shade through the shared lighting include with a camera-facing normal for billboards and ribbons; plain fog and volumetric fog reach them through the fog helper.
- **Emitted lights** go into the light buffer before the light clusters are built, under a cap in the renderer config, so they light the scene like point lights; GPU effects append theirs from compute.
- **Culling:** each emitter has bounds (fixed in the effect, or grown by the CPU simulation), culled per view.
- **Decals:** a sub-emitter target may be a decal once the decals spec is built; until then the field is absent.
- **C#:** effects defined in C#, `Particles.Burst`, `ParticleEmitter`, update callbacks for CPU effects, and the three events, through the generated bindings.
- **Shaders** for the new compute and draw programs are written in Slang (ADR-0073).
- **Glossary:** Particle effect and Particle emitter are already defined; *Ribbon* is already defined (added with the planning)

## Testing Decisions

- **A good test here checks particles and frames**: how many particles, where they are, what the graph runs, what reaches the screen; never how a module is coded.
- **CPU simulation tests**, headless at a fixed step with a fixed seed: spawn counts by rate, distance and bursts; lifetimes and death; forces and noise; over-life curves and gradients; capacity; world and local space; sub-emitters on birth, death and collision; events reaching their readers; the update callback seeing particles; pause and time scale.
- **CPU and GPU agreement**, a GL test (label `gl`): the same effect, seed and steps on both, the GPU particles read back and compared with the CPU ones within a tolerance, for spawning, forces, over-life change and depth-free collision with shapes.
- **Null-backend renderer tests:** GPU emitters' compute passes run before the cull; GPU-written instance lists are drawn indirectly; billboards, ribbons and meshes produce their draws in the right pass; alpha particles are sorted and additive ones are not; soft particles bind scene depth; lit particles bind the light clusters; emitted lights reach the light buffer up to the cap; emitters out of view are culled.
- **Physics glue tests:** CPU particles hitting a static collider bounce, slide or die as configured and send collision events.
- **GL golden images**, at a fixed seed and frame: a billboard burst, a ribbon trail, and lit and light-emitting particles.
- **FenrirRendererScripting's fixture tests:** an effect defined in C#, a burst, an emitter, a CPU update callback and the events read in C#.
- **By hand, a new ParticleDemo app** on the demo scaffold, with scenes for bursts, a GPU fountain, ribbons, mesh particles, lit and light-emitting particles, and physics collision, each with keys for its effect's main parameters and its CPU or GPU switch where both apply. It installs physics for the collision scene, and its smoke runs cover every scene on the null backend.

## Out of Scope

- Game-written GPU modules and an open module graph.
- Events and readback from GPU particles.
- Decals (their own spec), and a decal sub-emitter until that spec is built.
- Particle authoring tools and an editor.
- Weather systems built from particles (the game's).
- Audio for effects.

## Further Notes

- The facts behind the decisions are in [How do engines split particles between the engine and the game?](../engine-gaps/issues/24-how-engines-split-particles.md) (findings on the local `research/engine-particles` branch) and [What is the engine's particle system?](../engine-gaps/issues/25-what-the-particle-system-is.md).
- It uses the time scale spec's game time, and, where they are built first, the game passes spec's depth resolve, the plain and volumetric fog helper, and the per-entity spec's per-draw colour. The device's storage images from the volumetric fog spec are not needed here (particles use storage buffers).
- The tickets are one stack numbered in build order: CPU billboards and bursts, and C# bursts, first, as decided, since that is what Flightless Frenzy needs; then GPU simulation; then sub-emitters and events, ribbons, mesh particles, lit and light-emitting particles; collision, CPU and GPU, last.
- Build and test only `msvc-x64-debug` locally, run the GL tests on your own GPU before a PR that touches the OpenGL backend, and use the short-path junction in a worktree (`README.md`, "Building and testing").
- ADR-0089 is `proposed` until ticket 12, the spec's last, makes it `accepted` (CODING_STANDARDS.md).
