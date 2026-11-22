# 06: Ribbons

Spec: [Particles](../spec.md). ADR-0089.

**What to build:** CPU effects can render as ribbons: an emitter's particles joined in spawn order into a camera-facing strip, with width over life, colour along the strip and a texture along or tiled. ParticleDemo gains a Ribbons scene of trails behind moving emitters.

**Blocked by:** 02
**Touches:** `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/api/`, `packages/FenrirAssets/`, `examples/ParticleDemo/`

**Status:** ready-for-agent

Stacked on 05, on the one stack 01 to 12, in build order as decided: CPU bursts and C# bursts, then GPU simulation, then sub-emitters and events, ribbons, mesh particles, lit and light-emitting particles, and collision last.

- [ ] A CPU test of the strip built from particles in spawn order, with its widths and colours.
- [ ] Null-backend tests: a ribbon emitter produces one strip draw in the transparent pass.
- [ ] ParticleDemo's Ribbons scene works by hand; smoke runs pass.
- [ ] The API description and generated C# bindings are current with this ticket's modules.
