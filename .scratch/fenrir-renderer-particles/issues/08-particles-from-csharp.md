# 08: Particles from C#

Spec: [Particles](../spec.md). ADR-0089.

**What to build:** A C# script defines particle effects with every CPU-side module, plays bursts with `Particles.Burst`, adds and controls `ParticleEmitter` components, sets an update callback on a CPU effect, and reads the three particle events, through the renderer's scripting extension and the generated bindings.

**Blocked by:** 03, 06
**Touches:** `packages/FenrirRenderer/Scripting/`, `packages/FenrirRenderer/Managed/`

**Status:** ready-for-agent

Branches from 06 and runs beside 09 to 12 (its files are the scripting extension's).

- [ ] FenrirRendererScripting fixture tests: an effect defined in C# plays; a burst spawns; an emitter starts and stops; a C# update callback moves particles; events are read in C#.
- [ ] The generated bindings are current.
