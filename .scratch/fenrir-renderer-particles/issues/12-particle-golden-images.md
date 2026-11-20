# 12: Particle golden images

Spec: [Particles](../spec.md). ADR-0077.

**What to build:** GL golden-image tests, at a fixed seed and frame, show a billboard burst, a ribbon trail, and lit and light-emitting particles.

**Blocked by:** 11
**Touches:** `packages/FenrirRenderer/Tests/`

**Status:** ready-for-agent

Top of the core stack, on 11.

- [ ] Three GL golden-image tests (label `gl`, skipped without a GPU) compare with committed goldens.
