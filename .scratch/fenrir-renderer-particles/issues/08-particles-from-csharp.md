# 08: Particles from C#

Spec: [Particles](../spec.md). ADR-0089.

**What to build:** A C# script defines particle effects, plays bursts with `Particles.Burst`, and adds and controls `ParticleEmitter` components, through the renderer's scripting extension and the generated bindings, so a C#-only game fires its bursts as soon as CPU bursts exist. Modules added by later tickets reach C# through the API description as each ticket regenerates the bindings; events and update callbacks reach C# with ticket 03.

**Blocked by:** 02
**Touches:** `packages/FenrirRenderer/Scripting/`, `packages/FenrirRenderer/Managed/`

**Status:** ready-for-agent

Branches from 02 and runs beside the core stack (its files are the scripting extension's).

- [ ] FenrirRendererScripting fixture tests: an effect defined in C# plays; a burst spawns; an emitter starts and stops.
- [ ] The generated bindings are current.
