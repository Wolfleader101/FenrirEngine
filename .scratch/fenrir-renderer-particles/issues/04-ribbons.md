# 04: Ribbons

Spec: [Particles](../spec.md). ADR-0077.

**What to build:** CPU effects can render as ribbons: an emitter's particles joined in spawn order into a camera-facing strip, with width over life, colour along the strip and a texture along or tiled. ParticleDemo gains a Ribbons scene of trails behind moving emitters.

**Blocked by:** 02
**Touches:** `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/api/`, `packages/FenrirAssets/`, `examples/ParticleDemo/`

**Status:** ready-for-agent

Stacked on 03. On the core stack 01, 02, 03, 04, 05, 06, 09, 10, 11, 12.

- [ ] A CPU test of the strip built from particles in spawn order, with its widths and colours.
- [ ] Null-backend tests: a ribbon emitter produces one strip draw in the transparent pass.
- [ ] ParticleDemo's Ribbons scene works by hand; smoke runs pass.
