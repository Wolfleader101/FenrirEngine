# 03: Particles from C#

Spec: [Particles](../spec.md). ADR-0089.

**What to build:** A C# script defines particle effects, plays bursts with `Particles.Burst`, and adds and controls `ParticleEmitter` components, through the renderer's scripting extension and the generated bindings, so a C#-only game fires its bursts as soon as CPU bursts exist. Modules added by later tickets reach C# through the API description as each ticket regenerates the bindings; events and update callbacks reach C# with ticket 05.

**Blocked by:** 02
**Touches:** `packages/FenrirRenderer/Scripting/`, `packages/FenrirRenderer/Managed/`

**Status:** ready-for-agent

Stacked on 02, on the one stack 01 to 12, in build order as decided: CPU bursts and C# bursts, then GPU simulation, then sub-emitters and events, ribbons, mesh particles, lit and light-emitting particles, and collision last.

- [ ] FenrirRendererScripting fixture tests: an effect defined in C# plays; a burst spawns; an emitter starts and stops.
- [ ] The generated bindings are current.
