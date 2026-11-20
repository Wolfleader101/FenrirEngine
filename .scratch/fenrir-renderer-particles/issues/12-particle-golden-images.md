# 12: Particle golden images

Spec: [Particles](../spec.md). ADR-0089.

**What to build:** Last of the spec: GL golden-image tests, at a fixed seed and frame, show a billboard burst, a ribbon trail, and lit and light-emitting particles.

**Blocked by:** 03, 09, 10, 11
**Touches:** `docs/adr/0089-*.md`, `packages/FenrirRenderer/Tests/`

**Status:** ready-for-agent

Stacked on 11, on the one stack 01 to 12, in build order as decided: CPU bursts and C# bursts, then GPU simulation, then sub-emitters and events, ribbons, mesh particles, lit and light-emitting particles, and collision last.

- [ ] Three GL golden-image tests (label `gl`, skipped without a GPU) compare with committed goldens.
- [ ] ADR-0089's status becomes `accepted`.
